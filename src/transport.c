/* Implements the transport layer for RPC on top of the link layer.
   It builds request/response/error messages, dispatches incoming
   requests to registered callbacks, and provides a synchronous call API.

   Request  : [type=0x0B][counter][name ASCIIZ][args...]
   Stream   : [type=0x0C][counter][name ASCIIZ][args...]
   Response : [type=0x16][counter][data...]
   Error    : [type=0x21][counter][error_code] */

#include "transport.h"
#include "link_layer.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <string.h>
#include <stddef.h>

// Maximum number of functions that can be registered
#define MAX_FUNCTIONS 8


// Function registry entry
typedef struct {
    char *name;
    rpc_callback_t callback;
} rpc_entry_t;


// Function registry storage
static rpc_entry_t function_registry[MAX_FUNCTIONS];
static size_t registry_count = 0;

// Single outstanding call state
static uint8_t current_counter = 0;          // last issued counter value
static QueueHandle_t pending_queue = NULL;   // queue delivering a single response pointer
static SemaphoreHandle_t pending_mutex;      // protects pending_queue and current_counter

// RX task prototype
static void transport_receiver_task(void *arg);


// Initialize transport: create mutex and spawn the RX task
void transport_init(void) {
    pending_mutex = xSemaphoreCreateMutex();
    (void)xTaskCreate(transport_receiver_task, "rpc_rx", 4096, NULL, 10, NULL);
}


// Register a function by name; copies name and stores callback
int transport_register_function(const char *name, rpc_callback_t callback) {
    if (!name || !callback) return -1;
    if (registry_count >= MAX_FUNCTIONS) return -2;

    size_t nlen = strlen(name);
    char *copy = (char *)pvPortMalloc(nlen + 1);
    if (!copy) return -3;

    memcpy(copy, name, nlen);
    copy[nlen] = '\0';

    function_registry[registry_count].name = copy;
    function_registry[registry_count].callback = callback;
    registry_count++;
    return 0;
}


// Find a registered function by (name, length) pair
static rpc_entry_t *find_function(const char *name, uint8_t name_len) {
    for (size_t i = 0; i < registry_count; i++) {
        if (strlen(function_registry[i].name) == name_len &&
            strncmp(function_registry[i].name, name, name_len) == 0) {
            return &function_registry[i];
        }
    }
    return NULL;
}


// Perform a synchronous RPC call.
// Builds a request, sends it via link layer, then waits for a response/error.
int transport_call(const char *name, const uint8_t *args, uint16_t args_len,
                   uint8_t **response, uint16_t *resp_len,
                   uint8_t *error_code, uint32_t timeout_ms)
{
    if (!name || !response || !resp_len || !error_code) return -1;

    // Ensure only one outstanding call is active
    if (xSemaphoreTake(pending_mutex, portMAX_DELAY) != pdTRUE) return -2;
    if (pending_queue != NULL) {
        xSemaphoreGive(pending_mutex);
        return -3; // another call is pending
    }

    pending_queue = xQueueCreate(1, sizeof(void *));
    if (!pending_queue) {
        xSemaphoreGive(pending_mutex);
        return -4;
    }

    // Build request payload: [type][counter][name][0][args...]
    current_counter++;
    uint8_t counter = current_counter;
    uint8_t name_len = (uint8_t)strlen(name);

    uint16_t payload_len = (uint16_t)(2 + name_len + 1 + args_len);
    uint8_t *payload = (uint8_t *)pvPortMalloc(payload_len);

    if (!payload) {
        vQueueDelete(pending_queue);
        pending_queue = NULL;
        xSemaphoreGive(pending_mutex);
        return -5;
    }

    size_t off = 0;
    payload[off++] = MSG_TYPE_REQUEST;
    payload[off++] = counter;
    memcpy(payload + off, name, name_len);
    off += name_len;
    payload[off++] = 0x00; // name terminator
    if (args_len > 0 && args) memcpy(payload + off, args, args_len);

    // Send over link layer
    int rc = link_send_frame(payload, payload_len);
    vPortFree(payload);
    if (rc != 0) {
        vQueueDelete(pending_queue);
        pending_queue = NULL;
        xSemaphoreGive(pending_mutex);
        return -6;
    }

    // Release mutex while waiting
    xSemaphoreGive(pending_mutex);

    // Wait for response pointer from RX task
    void *resp_ptr = NULL;
    if (xQueueReceive(pending_queue, &resp_ptr, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        // timeout: clean up pending state
        xSemaphoreTake(pending_mutex, portMAX_DELAY);
        vQueueDelete(pending_queue);
        pending_queue = NULL;
        xSemaphoreGive(pending_mutex);
        return -7;
    }

    // Response envelope: [len_low][len_high][error][data...]
    uint8_t *resp_buf = (uint8_t *)resp_ptr;
    uint16_t datalen = (uint16_t)resp_buf[0] | ((uint16_t)resp_buf[1] << 8);
    *error_code = resp_buf[2];

    if (*error_code == 0) {
        *resp_len = datalen;
        if (datalen > 0) {
            *response = (uint8_t *)pvPortMalloc(datalen);
            if (*response) {
                memcpy(*response, resp_buf + 3, datalen);
            } else {
                vPortFree(resp_buf);
                return -8; // allocation failure for response
            }
        } else {
            *response = NULL;
        }
    } else {
        *resp_len = 0;
        *response = NULL;
    }

    vPortFree(resp_buf);

    // Clear pending state
    xSemaphoreTake(pending_mutex, portMAX_DELAY);
    vQueueDelete(pending_queue);
    pending_queue = NULL;
    xSemaphoreGive(pending_mutex);
    return 0;
}


// Helper: send error message [MSG_TYPE_ERROR][counter][error_code]
static void send_error_response(uint8_t counter, uint8_t error_code) {
    uint8_t payload[3];
    payload[0] = MSG_TYPE_ERROR;
    payload[1] = counter;
    payload[2] = error_code;
    (void)link_send_frame(payload, 3);
}


// Helper: send normal response [MSG_TYPE_RESPONSE][counter][data...]
static void send_response(uint8_t counter, const uint8_t *data, uint16_t len) {
    uint8_t *payload = (uint8_t *)pvPortMalloc((size_t)2 + len);

    if (!payload) {
        send_error_response(counter, ERR_INTERNAL);
        return;
    }

    payload[0] = MSG_TYPE_RESPONSE;
    payload[1] = counter;
    if (len > 0 && data) memcpy(payload + 2, data, len);
    
    (void)link_send_frame(payload, (uint16_t)(2 + len));
    vPortFree(payload);
}


// RX task: continuously receives link-layer frames and dispatches them
static void transport_receiver_task(void *arg) {
    (void)arg;

    uint8_t  rx_buffer[256];
    uint16_t rx_len;

    for (;;) {
        // wait for the next complete link-layer frame
        if (link_receive_frame(rx_buffer, sizeof(rx_buffer), &rx_len) != 0) continue;
        if (rx_len < 2) continue;

        uint8_t type    = rx_buffer[0];
        uint8_t counter = rx_buffer[1];

        switch (type) {
        case MSG_TYPE_REQUEST: {
            // parse request: [type][counter][name ASCIIZ][args...]
            if (rx_len < 3) {
                send_error_response(counter, ERR_INTERNAL);
                break;
            }

            // scan for terminating zero of the function name
            uint16_t i = 2;
            while (i < rx_len && rx_buffer[i] != 0x00) {
                i++;
            }
            if (i >= rx_len) {
                // no terminator found
                send_error_response(counter, ERR_INTERNAL);
                break;
            }

            uint8_t     name_len    = (uint8_t)(i - 2);
            const char *name        = (const char *)&rx_buffer[2];
            uint16_t    args_offset = (uint16_t)(i + 1);
            uint16_t    args_len    = (args_offset <= rx_len) ? (uint16_t)(rx_len - args_offset) : 0;
            const uint8_t *args     = (args_len > 0) ? &rx_buffer[args_offset] : NULL;

            // lookup and invoke the registered callback
            rpc_entry_t *entry = find_function(name, name_len);
            if (!entry) {
                send_error_response(counter, ERR_FUNC_NOT_FOUND);
                break;
            }

            uint8_t  *resp_data = NULL;
            uint16_t  resp_len  = 0;
            uint8_t   err_code  = 0;

            entry->callback(args, args_len, &resp_data, &resp_len, &err_code);

            if (err_code != 0) send_error_response(counter, err_code);
            else               send_response(counter, resp_data, resp_len);

            if (resp_data) vPortFree(resp_data);
            break;
        }

        case MSG_TYPE_RESPONSE:
        case MSG_TYPE_ERROR: {
            // deliver the result/error to the waiting caller (if any)
            xSemaphoreTake(pending_mutex, portMAX_DELAY);
            if (pending_queue && counter == current_counter) {
                uint16_t payload_len = 0;
                uint8_t  err         = 0;

                if (type == MSG_TYPE_ERROR) {
                    // error payload carries a single byte: error_code
                    err = (rx_len >= 3) ? rx_buffer[2] : ERR_INTERNAL;
                    payload_len = 0;
                } else {
                    // response payload starts at rx_buffer[2]
                    payload_len = (rx_len >= 2) ? (uint16_t)(rx_len - 2) : 0;
                    err = 0;
                }

                // envelope for the caller: [len_low][len_high][error][data...]
                uint8_t *copy = (uint8_t *)pvPortMalloc((size_t)2 + 1 + payload_len);
                if (copy) {
                    copy[0] = (uint8_t)(payload_len & 0xFF);
                    copy[1] = (uint8_t)((payload_len >> 8) & 0xFF);
                    copy[2] = err;
                    if (type == MSG_TYPE_RESPONSE && payload_len > 0) {
                        memcpy(copy + 3, &rx_buffer[2], payload_len);
                    }
                    (void)xQueueSend(pending_queue, &copy, 0);
                }
            }
            xSemaphoreGive(pending_mutex);
            break;
        }

        default:
            // unknown type: ignore and continue
            break;
        }
    }
    // unreachable
}
