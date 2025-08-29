/* High-level RPC transport on top of the link layer.
   Message layout (transport layer):
   Request  : [type=0x0B][counter][name ASCIIZ][args...]
   Stream   : [type=0x0C][counter][name ASCIIZ][args...]
   Response : [type=0x16][counter][data...]
   Error    : [type=0x21][counter][error_code] */

#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif


// Message type constants 
#define MSG_TYPE_REQUEST   0x0B
#define MSG_TYPE_RESPONSE  0x16
#define MSG_TYPE_ERROR     0x21

// Error codes carried by MSG_TYPE_ERROR
#define ERR_FUNC_NOT_FOUND 1
#define ERR_INTERNAL       2


/* Callback signature for registered RPC functions.
   args       - pointer to the raw argument bytes
   args_len   - number of bytes in args
   resp_data  - out pointer; callee allocates response buffer with malloc (or NULL)
   resp_len   - out length of the response in bytes
   error_code - out error code (0 means success) */
typedef void (*rpc_callback_t)(const uint8_t *args, uint16_t args_len,
                               uint8_t **resp_data, uint16_t *resp_len,
                               uint8_t *error_code);


// Initialize transport: creates internal mutex and spawns the RX task 
void transport_init(void);

/* Register an RPC function by ASCII name; name is copied internally.
   Returns 0 on success, negative on error. */
int transport_register_function(const char *name, rpc_callback_t callback);

/* Perform a synchronous RPC call.
   name        - function name (ASCIIZ)
   args        - raw arguments (can be NULL if args_len == 0)
   args_len    - number of bytes in args
   response    - out pointer to malloc'ed response buffer (NULL if error)
   resp_len    - out number of bytes in response
   error_code  - out error code (0 on success, nonzero on error)
   timeout_ms  - timeout for waiting a response
   Returns 0 on success (a response/error was delivered), negative on error. */
int transport_call(const char *name, const uint8_t *args, uint16_t args_len,
                   uint8_t **response, uint16_t *resp_len,
                   uint8_t *error_code, uint32_t timeout_ms);


#ifdef __cplusplus
}
#endif
