#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Calculate CRC-16/CCITT-FALSE checksum.
 *
 * Polynomial : 0x1021
 * Initial value : 0xFFFF
 * Input / output reflection : none
 *
 * @param data   Pointer to data buffer.
 * @param length Number of bytes.
 * @return       16-bit CRC value.
 */
uint16_t calculateCRC16(const uint8_t *data, size_t length);
