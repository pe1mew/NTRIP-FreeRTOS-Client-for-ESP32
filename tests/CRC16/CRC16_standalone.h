#ifndef CRC16_STANDALONE_H
#define CRC16_STANDALONE_H

#include <cstdint>
#include <stddef.h>

/**
 * \brief Function to calculate CRC-16 for the given data.
 * \param[in] data Pointer to the data buffer.
 * \param[in] length Length of the data buffer.
 * \return Calculated CRC-16 value.
 */
uint16_t calculateCRC16(const uint8_t* data, size_t length);

#endif // CRC16_STANDALONE_H
