
/*!
 * \file NTRIPClientCRC16.h
 * \brief Header file for CRC-16 calculation.
 *
 * This file contains the function declaration for calculating CRC-16.
 *  
 * \author Remko Welling (remko.welling@han.nl)
 * \date 30-3-2025
 *
 * \note This implementation is based on the CRC-16-CCITT (XMODEM) algorithm.
 *
 * \section crc16_param CRC-16 Parameterization
 * The implemented CRC calculation is also known as: 
 *  - CRC-16/IBM-3740
 *  - CRC-16/AUTOSAR
 *  - CRC-16/CCITT-FALSE
 * 
 * \section Parameterization Details
 * CRC-16-CCITT (XMODEM) implementation is based on the following parameters:
 *  - Width: 16 bits
 *  - Polynomial: 0x1021
 *  - Initial Value: 0xFFFF
 *  - Input Reflected: No
 *  - Output Reflected: No
 *  - Final XOR Value: 0x0000
 *
 * \section crc16_verification Verification
 * To verify this parameterization, you can use an online 
 * CRC calculator (e.g., https://crccalc.com/) with the
 * following settings:
 * - Polynomial: 0x1021
 * - Initial Value: 0xFFFF
 * - Input Reflected: No
 * - Output Reflected: No
 * - Final XOR Value: 0x0000
 */

#ifndef NTRIPCLIENTCRC16_H
#define NTRIPCLIENTCRC16_H

#include <cstdint> // For fixed-width integer types like uint16_t
#include <stddef.h>

/**
 * \brief Function to calculate CRC-16 for the given data.
 * \param[in] data Pointer to the data buffer.
 * \param[in] length Length of the data buffer.
 * \return Calculated CRC-16 value.
 */
extern uint16_t calculateCRC16(const uint8_t* data, size_t length);

#endif // NTRIPCLIENTCRC16_H

