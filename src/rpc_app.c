/* Provides example RPC functions: ping, sum and echo.  New functions may be
   added by following the same pattern and registering them in rpc_app_init(). */

#include "rpc_app.h"
#include "transport.h"
#include <freertos/FreeRTOS.h>   
#include <string.h>
#include <stdint.h>
#include <stdio.h>


static void rpc_sum(const uint8_t *args, uint16_t args_len,
                    uint8_t **resp_data, uint16_t *resp_len, uint8_t *error_code);

static void rpc_echo(const uint8_t *args, uint16_t args_len,
                     uint8_t **resp_data, uint16_t *resp_len, uint8_t *error_code);

// Register demo functions.  Ownership: transport layer keeps the registry. 
void rpc_app_init(void) {
    (void)transport_register_function("sum",  rpc_sum);
    (void)transport_register_function("echo", rpc_echo);
}


/* sum: read two little-endian uint32_t arguments, compute sum,
   format decimal ASCII into a heap buffer and return it. */
static void rpc_sum(const uint8_t *args, uint16_t args_len,
                    uint8_t **resp_data, uint16_t *resp_len, uint8_t *error_code) {
    // Validate input contract 
    if (!resp_data || !resp_len || !error_code) {
        return; // caller misused API; nothing we can safely do 
    }
    if (args_len != 8 || !args) {
        *error_code = ERR_INTERNAL;
        *resp_data = NULL; *resp_len = 0;
        return;
    }

    // Decode little-endian uint32_t values 
    uint32_t a = (uint32_t)args[0]
               | ((uint32_t)args[1] << 8)
               | ((uint32_t)args[2] << 16)
               | ((uint32_t)args[3] << 24);

    uint32_t b = (uint32_t)args[4]
               | ((uint32_t)args[5] << 8)
               | ((uint32_t)args[6] << 16)
               | ((uint32_t)args[7] << 24);

    uint32_t result = a + b;

    // Format decimal ASCII into a small stack buffer first 
    char tmp[12]; // up to 10 digits for 32-bit unsigned + '\0' headroom 
    int written = snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)result);
    if (written <= 0) {
        *error_code = ERR_INTERNAL;
        *resp_data = NULL; 
        *resp_len = 0;
        return;
    }

    // Allocate exact-size heap buffer 
    char *buf = (char *)pvPortMalloc((size_t)written + 1u);
    if (!buf) {
        *error_code = ERR_INTERNAL;
        *resp_data = NULL; 
        *resp_len = 0;
        return;
    }

    // Produce response payload 
    memcpy(buf, tmp, (size_t)written);
    buf[written] = '\0';

    *resp_data  = (uint8_t *)buf;
    *resp_len   = (uint16_t)written; 
    *error_code = 0;
}

// echo: copy input bytes to heap buffer and append '\0' sentinel. 
static void rpc_echo(const uint8_t *args, uint16_t args_len,
                     uint8_t **resp_data, uint16_t *resp_len, uint8_t *error_code)
{
    // Validate output pointers 
    if (!resp_data || !resp_len || !error_code) {
        return;
    }

    // Allocate (args_len + 1) to append '\0' for convenience 
    uint8_t *buf = (uint8_t *)pvPortMalloc((size_t)args_len + 1u);
    if (!buf) {
        *error_code = ERR_INTERNAL;
        *resp_data = NULL; 
        *resp_len = 0;
        return;
    }

    if (args_len > 0 && args) {
        memcpy(buf, args, args_len);
    }
    buf[args_len] = '\0';

    *resp_data  = buf;
    *resp_len   = args_len; 
    *error_code = 0;
}
