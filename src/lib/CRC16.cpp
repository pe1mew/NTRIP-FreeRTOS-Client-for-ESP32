#include <cstdint>
#include <stddef.h>

#include "CRC16.h"

uint16_t calculateCRC16(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF; // Initial value
    const uint16_t polynomial = 0x1021; // CRC-16-CCITT polynomial

    for (size_t i = 0; i < length; i++) {
        crc ^= (data[i] << 8); // XOR byte into the top of crc

        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ polynomial;
            } else {
                crc = (crc << 1);
            }
        }
    }

    return crc;
}
