/* Entry point for the ESP-IDF application.  Performs initialisation and
   spawns a demo task that exercises the RPC protocol by calling the
   registered functions locally.  In a real system the peer would be a
   remote device using the same protocol. */

#include "physical.h"
#include "link_layer.h"
#include "transport.h"
#include "rpc_app.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <stdio.h>


// Forward declaration of the demo RPC client task. 
static void rpc_client_demo(void *arg);


void app_main(void) {
    // Initialise lower layers and register application functions. 
    physical_init();
    link_init();
    transport_init();
    rpc_app_init();

    // Create a task to demonstrate RPC calls. 
    (void)xTaskCreate(rpc_client_demo, "rpc_demo", 4096, NULL, 5, NULL);
}


// Demo task: call sum and echo against the local RPC server. 
static void rpc_client_demo(void *arg) {
    (void)arg;

    // Give lower layers time to start
    vTaskDelay(pdMS_TO_TICKS(2000));

    uint8_t  *response = NULL;  // transport_call() allocates with pvPortMalloc 
    uint16_t  resp_len  = 0;
    uint8_t   err       = 0;

    // Example 1: call sum (adds 1 + 2).  Response is decimal string. 
    uint8_t sum_args[8] = {1,0,0,0,  2,0,0,0};

    if (transport_call("sum",
                       sum_args, sizeof(sum_args),
                       &response, &resp_len, &err,
                       5000) == 0 && err == 0) {
        // Copy response to a temporary null-terminated string for strtoul(). 
        char *tmp = (char *)pvPortMalloc((size_t)resp_len + 1u);
        if (tmp) {
            memcpy(tmp, response, resp_len);
            tmp[resp_len] = '\0';
            unsigned long sum_result = strtoul(tmp, NULL, 10);
            printf("sum response: %lu\n", sum_result);
            vPortFree(tmp);
        }
        // Free the response buffer allocated by transport (FreeRTOS heap). 
        vPortFree(response);
        response = NULL;

    } else {
        printf("sum call failed, err=%u\n", (unsigned)err);
    }

    // Example 2: call echo with a short string. 
    const char *msg = "hello";
    if (transport_call("echo",
                       (const uint8_t *)msg, (uint16_t)strlen(msg),
                       &response, &resp_len, &err,
                       5000) == 0 && err == 0)
    {
        // Print response as a bounded string (resp_len chars). 
        printf("echo response: %.*s\n", (int)resp_len, (const char *)response);
        vPortFree(response);
        response = NULL;
        
    } else {
        printf("echo call failed, err=%u\n", (unsigned)err);
    }

    // End demo task. 
    vTaskDelete(NULL);
}
