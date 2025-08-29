// This file implements the link-layer framing logic.  

#include "link_layer.h"
#include "physical.h"
#include <string.h>
#include <freertos/FreeRTOS.h>


// Compute CRC-8 using polynomial x^8 + x^2 + x + 1 (0x07)
static uint8_t crc8_calc(const uint8_t *data, size_t length) {
    uint8_t crc = 0;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}


/* Incrementally update a CRC-8 value with one byte.  Uses the same
   polynomial (0x07) as crc8_calc(). */
static uint8_t crc8_update(uint8_t crc, uint8_t byte) {
    crc ^= byte;
    for (int j = 0; j < 8; j++) {
        crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}


void link_init(void) {
    // nothing to init here
}


int link_send_frame(const uint8_t *payload, uint16_t length) {
    // Disallow NULL payload when length is non-zero.
    if (!payload && length > 0) {
        return -1;
    }

    // Frame structure:
    // [0]     = LINK_START_BYTE
    // [1]     = len_low
    // [2]     = len_high
    // [3]     = header_crc
    // [4]     = LINK_DATA_START_BYTE
    // [5..]   = payload (length bytes)
    // [5+len] = full_crc
    // [6+len] = LINK_STOP_BYTE

    size_t frame_len = (size_t)length + 7; // 3 header + hdr_crc + data_start + payload + crc + stop
    uint8_t *frame = (uint8_t *)pvPortMalloc(frame_len);   
    if (!frame) {
        return -2;
    }

    // Populate header bytes and compute header CRC.
    frame[0] = LINK_START_BYTE;
    frame[1] = (uint8_t)(length & 0xFF);        // low byte
    frame[2] = (uint8_t)((length >> 8) & 0xFF); // high byte
    uint8_t hdr_crc = crc8_calc(frame, 3);
    frame[3] = hdr_crc;

    // Start of data byte
    frame[4] = LINK_DATA_START_BYTE;

    // Copy payload if any
    if (length > 0) {
        memcpy(frame + 5, payload, length);
    }

    // Compute full packet CRC over all bytes up to and including payload (start, len, hdr_crc, data_start, payload).
    uint8_t full_crc = 0;
    for (size_t i = 0; i < (size_t)length + 5; i++) {
        full_crc = crc8_update(full_crc, frame[i]);
    }
    frame[5 + length] = full_crc;

    // Stop byte
    frame[6 + length] = LINK_STOP_BYTE;

    // Send via physical layer
    int written = physical_send(frame, frame_len);
    vPortFree(frame);   

    return (written == (int)frame_len) ? 0 : -3;
}


/* Receive a framed package.  Returns 0 on success, negative on error.
   The state machine below processes bytes one at a time.  It will
   discard invalid frames and continue searching for the next valid
   start byte. */
int link_receive_frame(uint8_t *buffer, uint16_t buffer_size, uint16_t *out_len) {
    if (!buffer || !out_len) {
        return -1;
    }

    // States for the receive state machine
    enum {
        ST_WAIT_START = 0,
        ST_LEN_LOW,
        ST_LEN_HIGH,
        ST_HDR_CRC,
        ST_DATA_START,
        ST_PAYLOAD,
        ST_FULL_CRC,
        ST_STOP
    } state = ST_WAIT_START;

    uint8_t byte = 0;
    uint16_t length = 0;
    uint16_t idx = 0;
    uint8_t hdr[3];
    uint8_t hdr_crc_read = 0;
    uint8_t full_crc_calc = 0;
    uint8_t full_crc_read = 0;

    // Loop indefinitely until we return with a valid frame or error
    for (;;) {
        if (physical_receive_byte(&byte) < 0) {
            // No data available or I/O error
            return -2;
        }

        switch (state) {
        case ST_WAIT_START:
            if (byte == LINK_START_BYTE) {
                // Reset CRC calculation and store header byte 0
                full_crc_calc = 0;
                full_crc_calc = crc8_update(full_crc_calc, byte);
                hdr[0] = byte;
                state = ST_LEN_LOW;
            }
            break;

        case ST_LEN_LOW:
            hdr[1] = byte;
            full_crc_calc = crc8_update(full_crc_calc, byte);
            length = (uint16_t)byte; // low byte
            state = ST_LEN_HIGH;
            break;

        case ST_LEN_HIGH:
            hdr[2] = byte;
            full_crc_calc = crc8_update(full_crc_calc, byte);
            length |= ((uint16_t)byte << 8);
            state = ST_HDR_CRC;
            break;

        case ST_HDR_CRC:
            hdr_crc_read = byte;
            if (crc8_calc(hdr, 3) != hdr_crc_read) {
                state = ST_WAIT_START;
            } else {
                full_crc_calc = crc8_update(full_crc_calc, hdr_crc_read);
                state = ST_DATA_START;
            }
            break;

        case ST_DATA_START:
            if (byte == LINK_DATA_START_BYTE) {
                full_crc_calc = crc8_update(full_crc_calc, byte);
                idx = 0;
                state = (length == 0) ? ST_FULL_CRC : ST_PAYLOAD;
            } else {
                state = ST_WAIT_START;
            }
            break;

        case ST_PAYLOAD:
            if (idx < length) {
                if (idx < buffer_size) {
                    buffer[idx] = byte;
                }
                full_crc_calc = crc8_update(full_crc_calc, byte);
                idx++;
                if (idx >= length) {
                    state = ST_FULL_CRC;
                }
            }
            break;

        case ST_FULL_CRC:
            full_crc_read = byte;
            if (length <= buffer_size && full_crc_calc == full_crc_read) {
                state = ST_STOP;
            } else {
                state = ST_WAIT_START;
            }
            break;

        case ST_STOP:
            if (byte == LINK_STOP_BYTE) {
                *out_len = length;
                return 0;
            }
            state = ST_WAIT_START;
            break;
        }
    }

    return -3; // unreachable
}
