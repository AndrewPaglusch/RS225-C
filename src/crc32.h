/*******************************************************************************
 * CRC32.H - CRC32 Checksum Implementation
 *******************************************************************************
 * 
 * Standard CRC32 checksum algorithm for data integrity verification.
 * Uses polynomial 0xEDB88320 (reversed IEEE 802.3 polynomial).
 * 
 ******************************************************************************/

#ifndef CRC32_H
#define CRC32_H

#include "types.h"

/*
 * crc32 - Calculate CRC32 checksum
 * 
 * @param data   Data buffer to checksum
 * @param length Number of bytes to process
 * @return       32-bit CRC32 checksum
 * 
 * Standard CRC32 algorithm (IEEE 802.3)
 */
u32 crc32(const u8* data, size_t length);

#endif /* CRC32_H */
