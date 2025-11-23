/*******************************************************************************
 * CRC32.C - CRC32 Checksum Implementation
 *******************************************************************************
 *
 * PURPOSE:
 *   Implements the CRC32 (Cyclic Redundancy Check 32-bit) algorithm for data
 *   integrity verification. Used by player save system to detect file corruption
 *   from disk errors, incomplete writes, or bit flips.
 *
 * ALGORITHM: CRC32 (IEEE 802.3 / PKZIP / Ethernet standard)
 *   Polynomial: 0xEDB88320 (reversed representation of 0x04C11DB7)
 *   Initial value: 0xFFFFFFFF
 *   Final XOR: 0xFFFFFFFF (bitwise NOT of result)
 *   Reflect input: Yes (LSB first)
 *   Reflect output: Yes
 *
 * HOW CRC32 WORKS:
 *   CRC (Cyclic Redundancy Check) is a hash function that detects accidental
 *   changes to data. It works by treating data as a large polynomial and
 *   computing the remainder when divided by a generator polynomial.
 *
 *   MATHEMATICAL FOUNDATION:
 *     1. Treat input bytes as coefficients of polynomial in GF(2) field
 *     2. Divide input polynomial by generator polynomial
 *     3. Remainder is the CRC32 checksum
 *
 *   EXAMPLE: Computing CRC32 of "hello"
 *     Input bytes: [0x68, 0x65, 0x6c, 0x6c, 0x6f]
 *     Process each byte through lookup table
 *     Final result: 0x3610A686
 *
 * TABLE-DRIVEN OPTIMIZATION:
 *   Instead of computing polynomial division bit-by-bit (slow), we precompute
 *   a 256-entry lookup table. For each possible byte value (0-255), we store
 *   the CRC32 result of processing that byte. This reduces the algorithm from
 *   O(n × 8) bit operations to O(n) byte operations - roughly 8× faster.
 *
 *   LOOKUP TABLE GENERATION:
 *     For each byte value i (0-255):
 *       1. Start with crc = i
 *       2. For 8 bits:
 *          - If LSB is 1: crc = (crc >> 1) XOR polynomial
 *          - If LSB is 0: crc = (crc >> 1)
 *       3. Store result in table[i]
 *
 *   TABLE SIZE: 256 entries × 4 bytes = 1024 bytes (1 KB)
 *
 * DETECTION CAPABILITY:
 *   CRC32 can detect:
 *     ✓ All single-bit errors
 *     ✓ All double-bit errors
 *     ✓ All burst errors up to 32 bits
 *     ✓ ~99.9999% of longer burst errors
 *     ✓ ~99.9999% of random multi-bit errors
 *
 *   CRC32 cannot detect:
 *     ✗ Intentional tampering (not cryptographically secure)
 *     ✗ Specific adversarial bit patterns (collision attacks)
 *
 * PERFORMANCE:
 *   - Table initialization: O(256) - only done once, lazy initialization
 *   - Checksum computation: O(n) where n = data length
 *   - Memory: 1 KB for lookup table (static storage)
 *   - Typical speed: ~1-2 GB/sec on modern CPUs
 *
 * STANDARDS COMPLIANCE:
 *   - IEEE 802.3 (Ethernet frame check sequence)
 *   - ISO 3309 (HDLC)
 *   - PKZIP / ZIP file format
 *   - PNG image format
 *   - Gzip compression
 *
 * USAGE EXAMPLE:
 *   u8 data[] = "Hello, World!";
 *   u32 checksum = crc32(data, strlen((char*)data));
 *   printf("CRC32: 0x%08X\n", checksum);
 *   // Output: CRC32: 0xEC4AC3D0
 *
 * CROSS-REFERENCES:
 *   - Used by: player_save.c (save file integrity verification)
 *   - Standard: RFC 1952 (GZIP), ISO 3309, IEEE 802.3
 *   - Related: MD5, SHA-1 (cryptographic hashes - more secure but slower)
 *
 * THREAD SAFETY:
 *   - First call initializes table (not thread-safe initialization)
 *   - Subsequent calls are thread-safe (read-only table access)
 *   - Recommendation: Call crc32() once at startup to pre-initialize
 *
 ******************************************************************************/

#include "crc32.h"

/*
 * CRC32 lookup table - precomputed remainders for all byte values
 * 
 * This table contains the CRC32 remainder for processing each possible
 * byte value (0-255) with the IEEE 802.3 polynomial. Generated once
 * on first use by crc32_init_table().
 *
 * TABLE STRUCTURE:
 *   Index: Byte value (0-255)
 *   Value: CRC32 remainder for that byte (u32)
 *
 * EXAMPLE ENTRIES:
 *   table[0x00] = 0x00000000
 *   table[0x01] = 0x77073096
 *   table[0x02] = 0xEE0E612C
 *   ...
 *   table[0xFF] = 0x2D02EF8D
 */
static u32 crc32_table[256];
static bool table_initialized = false;

/*
 * crc32_init_table - Generate CRC32 lookup table for IEEE 802.3 polynomial
 *
 * Precomputes the CRC32 remainder for all possible byte values (0-255).
 * This table is used to accelerate CRC32 computation from O(n×8) to O(n).
 *
 * ALGORITHM:
 *   For each byte value i (0-255):
 *     1. Initialize crc = i
 *     2. For each of 8 bits:
 *        a. If LSB is set (crc & 1):
 *           - Shift right 1 bit
 *           - XOR with polynomial (0xEDB88320)
 *        b. Otherwise:
 *           - Just shift right 1 bit
 *     3. Store final crc in table[i]
 *
 * POLYNOMIAL: 0xEDB88320
 *   This is the bit-reversed form of the standard polynomial 0x04C11DB7.
 *   Reversing allows processing bytes LSB-first (little-endian style).
 *
 *   Standard:  0x04C11DB7 = 0000 0100 1100 0001 0001 1101 1011 0111
 *   Reversed:  0xEDB88320 = 1110 1101 1011 1000 0011 0010 0000 0000
 *
 * EXAMPLE: Generating table[0x05]:
 *   Start: crc = 0x05 = 0000 0101
 *   Bit 0: LSB=1 → (0x05>>1) ^ 0xEDB88320 = 0x02 ^ 0xEDB88320 = 0xEDB88322
 *   Bit 1: LSB=0 → 0xEDB88322 >> 1 = 0x76DC4191
 *   Bit 2: LSB=1 → (0x76DC4191>>1) ^ 0xEDB88320 = 0x3B6E20C8 ^ 0xEDB88320 = 0xD6D6A3E8
 *   ... (continue for 8 bits total)
 *   Result: table[0x05] = 0x9B64C2B0
 *
 * COMPLEXITY: O(256 × 8) = O(2048) bit operations (constant time)
 * MEMORY: Writes 256 × 4 bytes = 1024 bytes
 *
 * THREAD SAFETY: Not thread-safe. Should be called only once on first use.
 */
static void crc32_init_table(void) {
    u32 polynomial = 0xEDB88320;  /* IEEE 802.3 polynomial (bit-reversed) */
    
    for (u32 i = 0; i < 256; i++) {
        u32 crc = i;
        
        /* Process 8 bits of this byte value */
        for (u32 j = 0; j < 8; j++) {
            if (crc & 1) {
                /* LSB is 1: shift right and XOR with polynomial */
                crc = (crc >> 1) ^ polynomial;
            } else {
                /* LSB is 0: just shift right */
                crc >>= 1;
            }
        }
        
        /* Store computed remainder in lookup table */
        crc32_table[i] = crc;
    }
    
    table_initialized = true;
}

/*
 * crc32 - Calculate CRC32 checksum of data buffer
 *
 * @param data    Pointer to data buffer to checksum
 * @param length  Number of bytes in buffer
 * @return        32-bit CRC checksum (IEEE 802.3 standard)
 *
 * ALGORITHM:
 *   1. Initialize CRC to 0xFFFFFFFF (all bits set)
 *   2. For each byte in input:
 *      a. XOR byte with low 8 bits of CRC
 *      b. Use result as index into lookup table
 *      c. Shift CRC right 8 bits
 *      d. XOR with table entry
 *   3. Invert final CRC (bitwise NOT) and return
 *
 * STEP-BY-STEP EXAMPLE: CRC32 of [0xAB, 0xCD]
 *   Initial: crc = 0xFFFFFFFF
 *   
 *   Byte 0 (0xAB):
 *     index = (0xFFFFFFFF ^ 0xAB) & 0xFF = 0x54
 *     crc = (0xFFFFFFFF >> 8) ^ table[0x54]
 *         = 0x00FFFFFF ^ 0x9B64C2B0
 *         = 0x9B9B3D4F
 *   
 *   Byte 1 (0xCD):
 *     index = (0x9B9B3D4F ^ 0xCD) & 0xFF = 0x82
 *     crc = (0x9B9B3D4F >> 8) ^ table[0x82]
 *         = 0x009B9B3D ^ 0x5C0A7B71
 *         = 0x5591004C
 *   
 *   Final: ~0x5591004C = 0xAA6EFFB3
 *
 * WHY THE INITIAL 0xFFFFFFFF?
 *   Starting with all 1s ensures that leading zero bytes affect the
 *   checksum. Without this, CRC32([0x00, 0xAB]) would equal CRC32([0xAB]).
 *
 * WHY THE FINAL INVERT (~crc)?
 *   Inverting the output ensures that trailing zero bytes affect the
 *   checksum. It's part of the IEEE 802.3 standard specification.
 *
 * LOOKUP TABLE MECHANICS:
 *   The table lookup replaces 8 iterations of the bit-by-bit algorithm:
 *   
 *   Without table (slow):
 *     for each of 8 bits in byte:
 *       if (crc & 1): crc = (crc >> 1) ^ polynomial
 *       else: crc = crc >> 1
 *   
 *   With table (fast):
 *     index = (crc ^ byte) & 0xFF
 *     crc = (crc >> 8) ^ table[index]
 *   
 *   This is ~8× faster because we process entire bytes at once.
 *
 * PERFORMANCE:
 *   - First call: ~2048 table generation ops + checksum
 *   - Subsequent calls: Pure O(n) throughput
 *   - Typical speed: 1-2 GB/sec (cache-friendly, no branches in loop)
 *
 * USAGE EXAMPLES:
 *   
 *   Example 1: Verify file integrity
 *     u8 file_data[1024];
 *     fread(file_data, 1, 1024, fp);
 *     u32 computed = crc32(file_data, 1024);
 *     u32 stored;
 *     fread(&stored, 4, 1, fp);
 *     if (computed != stored) {
 *         printf("File corrupted!\n");
 *     }
 *   
 *   Example 2: String checksum
 *     const char* str = "Hello, World!";
 *     u32 hash = crc32((u8*)str, strlen(str));
 *     printf("CRC32: 0x%08X\n", hash);
 *   
 *   Example 3: Incremental CRC (not supported by this implementation)
 *     // To compute CRC incrementally, expose the intermediate crc value
 *     // between calls. This implementation requires the entire buffer at once.
 *
 * COLLISION RESISTANCE:
 *   CRC32 has 2^32 possible values (~4.3 billion). For random data:
 *     - Collision probability for 2 items: ~1 in 2^32
 *     - Birthday paradox: 50% collision after ~2^16 items (65,536)
 *   
 *   For cryptographic security, use SHA-256 instead. CRC32 is only
 *   designed to detect accidental errors, not malicious tampering.
 *
 * CROSS-REFERENCES:
 *   - Called by: player_save() and player_load() in player_save.c
 *   - Standard: IEEE 802.3, ISO 3309, RFC 1952
 *   - Table gen: crc32_init_table() (internal, automatic)
 *
 * COMPLEXITY: O(n) where n = data length
 * THREAD SAFETY: Thread-safe after first call (read-only table access)
 */
u32 crc32(const u8* data, size_t length) {
    /* Lazy initialization: generate table on first use */
    if (!table_initialized) {
        crc32_init_table();
    }
    
    /* Start with all bits set (IEEE 802.3 standard) */
    u32 crc = 0xFFFFFFFF;
    
    /* Process each byte using table lookup */
    for (size_t i = 0; i < length; i++) {
        /* XOR byte with low 8 bits of CRC, use as table index */
        u8 index = (crc ^ data[i]) & 0xFF;
        
        /* Shift CRC right 8 bits and XOR with table entry */
        crc = (crc >> 8) ^ crc32_table[index];
    }
    
    /* Invert all bits and return (IEEE 802.3 standard) */
    return ~crc;
}
