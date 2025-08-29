/* Implements framing for the link‑layer protocol described in the test task.
   The frame format is:
   [0xFA][len_low][len_high][CRC_header][0xFB][payload][CRC_full][0xFE] */

#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif


// Constant values for framing bytes
#define LINK_START_BYTE          0xFA
#define LINK_DATA_START_BYTE     0xFB
#define LINK_STOP_BYTE           0xFE


void link_init(void);

int link_send_frame(const uint8_t *payload, uint16_t length);

int link_receive_frame(uint8_t *buffer, uint16_t buffer_size, uint16_t *out_len);

#ifdef __cplusplus
}
#endif