#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// UART configuration: adjust pins and baudrate for your board
#define PHYS_UART_NUM      UART_NUM_1
#define PHYS_UART_TX_PIN   17
#define PHYS_UART_RX_PIN   16
#define PHYS_UART_BAUDRATE 115200

// Initialize physical layer: configures UART
void physical_init(void);

// Write len bytes to UART. Returns number of bytes written or negative on error
int physical_send(const uint8_t *data, size_t len);

// Read one byte from UART. Blocks until data is available. Returns 1 or negative on error
int physical_receive_byte(uint8_t *byte);

#ifdef __cplusplus
}
#endif
