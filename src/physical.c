/* Physical layer implementation using ESP-IDF UART driver.
   Provides initialization, blocking send and receive functions. */

#include "physical.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>


static bool s_uart_installed = false;

// Initialize UART with 8N1 configuration and install driver
void physical_init(void) {
    uart_config_t cfg = {
        .baud_rate = PHYS_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    uart_param_config(PHYS_UART_NUM, &cfg);

    uart_set_pin(PHYS_UART_NUM, PHYS_UART_TX_PIN, PHYS_UART_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    uart_driver_install(PHYS_UART_NUM,
                        256,  // RX buffer size
                        0,    // TX buffer size (send is synchronous)
                        0, NULL, 0);

    s_uart_installed = true;
}

// Send len bytes over UART. Blocks until all bytes are written
int physical_send(const uint8_t *data, size_t len) {
    if (!s_uart_installed || !data) {
        return -1;
    }
    const int written = uart_write_bytes(PHYS_UART_NUM, (const char *)data, len);
    return written;
}

// Receive one byte over UART. Blocks until byte is available
int physical_receive_byte(uint8_t *byte) {
    if (!s_uart_installed || !byte) {
        return -1;
    }
    int ret = uart_read_bytes(PHYS_UART_NUM, byte, 1, portMAX_DELAY);
    return (ret == 1) ? 1 : -1;
}
