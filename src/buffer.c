/*******************************************************************************
 * BUFFER.C - Dynamic Byte Stream Implementation
 *******************************************************************************
 * 
 * This file implements a dynamically-resizing byte buffer with support for:
 *   - Byte-level and bit-level I/O
 *   - Multiple endianness modes
 *   - Stream cipher integration (ISAAC)
 *   - Variable-length packet framing
 * 
 * KEY ALGORITHMS IMPLEMENTED:
 *   1. Exponential capacity growth (amortized O(1) append)
 *   2. Bit packing/unpacking with arbitrary bit widths
 *   3. Endianness conversion for multi-byte primitives
 *   4. Variable-length header backfilling
 * 
 *   - Dynamic memory management
 *   - Amortized analysis
 *   - Bit manipulation
 *   - Network byte order
 *   - Buffer overflow prevention
 * 
 ******************************************************************************/

#include "buffer.h"
#include <stdlib.h>
#include <string.h>

/*******************************************************************************
 * LIFECYCLE MANAGEMENT
 ******************************************************************************/

/*
 * buffer_create - Allocate new StreamBuffer with initial capacity
 * 
 * ALGORITHM:
 *   1. Allocate StreamBuffer struct on heap
 *   2. Allocate data array of requested capacity
 *   3. Zero-initialize all memory
 *   4. Initialize cursor fields to 0
 * 
 * MEMORY ALLOCATION DIAGRAM:
 * ┌─────────────────────────────────────────────────────────────────────┐
 * │                          HEAP MEMORY                                │
 * ├─────────────────────────┬───────────────────────────────────────────┤
 * │  StreamBuffer Struct    │        Data Array                         │
 * │  (40 bytes on 64-bit)   │        (capacity bytes)                   │
 * ├─────────────────────────┼───────────────────────────────────────────┤
 * │  - data: ptr ────────────────>┌───┬───┬───┬───┬───┬─────────┬───┐   │
 * │  - capacity: N          │     │ 0 │ 0 │ 0 │ 0 │ 0 │   ...   │ 0 │   │
 * │  - position: 0          │     └───┴───┴───┴───┴───┴─────────┴───┘   │
 * │  - bit_position: 0      │                                           │
 * │  - cipher: NULL         │                                           │
 * │  - var_len_pos: 0       │                                           │
 * │  - var_len_kind: 0      │                                           │
 * └─────────────────────────┴───────────────────────────────────────────┘
 * 
 * FAILURE MODES:
 *   - Returns NULL if StreamBuffer allocation fails
 *   - Returns NULL if data array allocation fails (after freeing struct)
 * 
 * COMPLEXITY: O(capacity) time, O(capacity) space
 */
StreamBuffer* buffer_create(u32 capacity) {
    /* Allocate structure on heap */
    StreamBuffer* buf = (StreamBuffer*)malloc(sizeof(StreamBuffer));
    if (!buf) return NULL;  /* Out of memory */
    
    /* Allocate data array */
    buf->data = (u8*)malloc(capacity);
    if (!buf->data) { 
        free(buf);  /* Clean up partial allocation */
        return NULL;
    }
    
    /* Zero-initialize data array (prevents undefined behavior) */
    memset(buf->data, 0, capacity);
    
    /* Initialize fields */
    buf->capacity     = capacity;
    buf->position     = 0;
    buf->bit_position = 0;
    buf->cipher       = NULL;
    buf->var_len_pos  = 0;
    buf->var_len_kind = 0;
    
    return buf;
}

/*
 * buffer_destroy - Free all heap memory associated with buffer
 * 
 * ALGORITHM:
 *   1. Check for NULL (safe to call on NULL pointer)
 *   2. Free data array
 *   3. Free StreamBuffer struct
 * 
 * MEMORY DEALLOCATION:
 * 
 * Before:                      After:
 * ┌──────────┐                 ┌──────────┐
 * │ buf  ────┼──> [struct]     │ buf  ────┼──> NULL
 * └──────────┘         │       └──────────┘
 *                      └──> [data array]
 * 
 * SAFETY: Always checks for NULL before freeing
 * COMPLEXITY: O(1) time
 */
void buffer_destroy(StreamBuffer* buf) {
    if (!buf) return;  /* NULL-safe */
    
    free(buf->data);   /* Free data array first */
    free(buf);         /* Then free struct */
}

/*
 * buffer_skip - Advance read/write position by N bytes
 * 
 * @param buf    Buffer to modify
 * @param count  Number of bytes to skip (clamped to remaining bytes)
 * 
 * PURPOSE: Used to skip over unneeded packet data without copying
 * 
 * EXAMPLE:
 *   Initial state:  position=5, capacity=100
 *   buffer_skip(buf, 10)
 *   Final state:    position=15
 * 
 * BOUNDS CHECKING:
 *   If count > remaining bytes, clamps to remaining
 *   Prevents position from exceeding capacity
 * 
 * COMPLEXITY: O(1) time
 */
void buffer_skip(StreamBuffer* buf, u32 count) {
    if (!buf) return;
    
    /* Calculate remaining bytes */
    u32 rem = buffer_get_remaining(buf);
    
    /* Clamp count to prevent overflow */
    if (count > rem) count = rem;
    
    /* Advance cursor */
    buf->position += count;
}

/*
 * buffer_reset - Reset all cursors to beginning (no memory freed)
 * 
 * EFFECTS:
 *   - position = 0
 *   - bit_position = 0
 *   - var_len_pos = 0
 *   - var_len_kind = 0
 *   - Data array contents unchanged
 *   - Capacity unchanged
 * 
 * USE CASE: Reuse buffer for new packet without reallocation
 * 
 * COMPLEXITY: O(1) time
 */
void buffer_reset(StreamBuffer* buf) {
    if (!buf) return;
    
    buf->position     = 0;
    buf->bit_position = 0;
    buf->var_len_pos  = 0;
    buf->var_len_kind = 0;
}

/*******************************************************************************
 * CURSOR MANAGEMENT
 ******************************************************************************/

/*
 * buffer_get_position - Return current byte offset
 * 
 * COMPLEXITY: O(1) time
 */
u32 buffer_get_position(const StreamBuffer* buf) {
    if (!buf) return 0;
    return buf->position;
}

/*
 * buffer_set_position - Move cursor to specific byte offset
 * 
 * @param buf  Buffer to modify
 * @param pos  New position (clamped to [0, capacity])
 * 
 * SIDE EFFECTS:
 *   - Sets bit_position = position * 8 (maintains byte-bit alignment)
 * 
 * USE CASE: Seek to beginning for reading after writing
 * 
 * EXAMPLE:
 *   buffer_write_int(buf, 12345, BIG);  // position now at 4
 *   buffer_set_position(buf, 0);        // rewind to start
 *   u32 val = buffer_read_int(buf, BIG); // read back value
 * 
 * COMPLEXITY: O(1) time
 */
void buffer_set_position(StreamBuffer* buf, u32 pos) {
    if (!buf) return;
    
    /* Clamp position to capacity (prevent overflow) */
    if (pos > buf->capacity) pos = buf->capacity;
    
    buf->position = pos;
    
    /* Keep bit_position synchronized (8 bits per byte) */
    buf->bit_position = buf->position * 8;
}

/*
 * buffer_get_remaining - Calculate unread bytes
 * 
 * RETURN: capacity - position (or 0 if position >= capacity)
 * 
 * USE CASE: Check if enough data available before reading
 * 
 * COMPLEXITY: O(1) time
 */
u32 buffer_get_remaining(const StreamBuffer* buf) {
    if (!buf) return 0;
    
    /* Prevent underflow if position > capacity (shouldn't happen) */
    return (buf->capacity > buf->position) 
        ? (buf->capacity - buf->position) 
        : 0;
}

/*******************************************************************************
 * CIPHER INTEGRATION
 ******************************************************************************/

/*
 * buffer_set_cipher - Attach ISAAC cipher for encryption/decryption
 * 
 * @param buf     Buffer to modify
 * @param cipher  Pointer to initialized ISAAC cipher, or NULL to disable
 * 
 * NOTE: Cipher is NOT owned by buffer (no automatic cleanup)
 * 
 * COMPLEXITY: O(1) time
 */
void buffer_set_cipher(StreamBuffer* buf, ISAACCipher* cipher) {
    if (!buf) return;
    buf->cipher = cipher;
}

/*******************************************************************************
 * DYNAMIC MEMORY GROWTH
 ******************************************************************************/

/*
 * ensure_capacity - Ensure buffer has room for N more bytes (internal helper)
 * 
 * @param buf   Buffer to expand
 * @param need  Number of additional bytes required
 * 
 * ALGORITHM: Exponential growth (doubling)
 *   1. If current capacity sufficient, return immediately
 *   2. Start with current capacity (or 64 if empty)
 *   3. Double capacity until position + need fits
 *   4. Reallocate data array
 *   5. Zero-initialize new bytes
 * 
 * GROWTH STRATEGY DIAGRAM:
 * 
 * Initial:     [████████                    ]  capacity=8, need=10
 * Double once: [████████████████            ]  capacity=16, need=10 (still too small)
 * Double twice:[████████████████████████████]  capacity=32, need=10 (sufficient!)
 * 
 * WHY DOUBLING?
 *   - Amortized O(1) per insertion
 *   - Limits number of expensive realloc() calls
 *   - Trade-off: wastes up to 50% memory, but fast
 * 
 * AMORTIZED ANALYSIS:
 *   Inserting N elements:
 *   - Reallocations happen at sizes: 1, 2, 4, 8, 16, ..., N
 *   - Total bytes copied: 1 + 2 + 4 + ... + N = 2N - 1
 *   - Average cost per insertion: (2N - 1) / N ≈ 2 = O(1)
 * 
 * FAILURE MODE:
 *   If realloc() fails, returns immediately (buffer unchanged)
 *   Caller must check if write succeeded (position advanced)
 * 
 * COMPLEXITY:
 *   - Amortized O(1) time per call
 *   - Worst case O(n) when reallocation needed
 */
static inline void ensure_capacity(StreamBuffer* buf, u32 need) {
    /* Fast path: already have enough space */
    if (buf->position + need <= buf->capacity) return;
    
    /* Calculate new capacity using exponential growth */
    u32 oldcap = buf->capacity;
    u32 newcap = buf->capacity ? buf->capacity : 64;  /* Start at 64 if empty */
    
    /* Keep doubling until we have enough space */
    while (buf->position + need > newcap) {
        newcap <<= 1;  /* Equivalent to newcap *= 2, but faster */
    }
    
    /* Attempt reallocation */
    u8* nd = (u8*)realloc(buf->data, newcap);
    if (!nd) return;  /* Out of memory - fail silently (original data intact) */
    
    /* Update buffer fields */
    buf->data = nd;
    buf->capacity = newcap;
    
    /* Zero-initialize newly allocated bytes (prevents info leaks) */
    memset(buf->data + oldcap, 0, newcap - oldcap);
}

/*******************************************************************************
 * BYTE-LEVEL WRITE OPERATIONS
 ******************************************************************************/

/*
 * buffer_write_byte - Write single byte to buffer
 * 
 * @param buf    Buffer to write to
 * @param value  Byte value (0-255)
 * 
 * ALGORITHM:
 *   1. Ensure capacity for 1 byte
 *   2. Write byte at current position
 *   3. Increment position
 * 
 * VISUAL:
 * Before:  [0x10][0x20][0x00][0x00]  position=2, value=0xAB
 *                       ↑
 * After:   [0x10][0x20][0xAB][0x00]  position=3
 *                              ↑
 * 
 * COMPLEXITY: O(1) amortized
 */
void buffer_write_byte(StreamBuffer* buf, u8 value) {
    ensure_capacity(buf, 1);
    buf->data[buf->position++] = value;
}

/*
 * buffer_write_short - Write 16-bit integer with specified byte order
 * 
 * @param buf    Buffer to write to
 * @param value  16-bit value (0-65535)
 * @param order  BYTE_ORDER_BIG or BYTE_ORDER_LITTLE
 * 
 * ENDIANNESS EXPLAINED:
 * 
 * BIG-ENDIAN (Network Byte Order):
 *   Most significant byte first (MSB-first)
 *   Used by: Network protocols (TCP/IP), RuneScape protocol, PowerPC, SPARC
 *   Example: 0x1234 → [0x12][0x34]
 * 
 * LITTLE-ENDIAN (Intel Byte Order):
 *   Least significant byte first (LSB-first)
 *   Used by: x86, x86-64, ARM (often)
 *   Example: 0x1234 → [0x34][0x12]
 * 
 * BIT EXTRACTION:
 *   value = 0x1234 (binary: 0001 0010 0011 0100)
 *   
 *   High byte: (value >> 8) & 0xFF
 *            = (0x1234 >> 8) & 0xFF
 *            = 0x0012 & 0xFF
 *            = 0x12
 *   
 *   Low byte:  value & 0xFF
 *            = 0x1234 & 0xFF
 *            = 0x34
 * 
 * MEMORY LAYOUT:
 * 
 * value = 0xABCD, order = BIG
 * ┌──────────┬──────────┐
 * │  0xAB    │  0xCD    │  (high byte first)
 * └──────────┴──────────┘
 *  position  position+1
 * 
 * value = 0xABCD, order = LITTLE
 * ┌──────────┬──────────┐
 * │  0xCD    │  0xAB    │  (low byte first)
 * └──────────┴──────────┘
 *  position  position+1
 * 
 * COMPLEXITY: O(1) amortized
 */
void buffer_write_short(StreamBuffer* buf, u16 value, ByteOrder order) {
    ensure_capacity(buf, 2);
    
    if (order == BYTE_ORDER_BIG) {
        /* Big-endian: high byte first */
        buf->data[buf->position++] = (u8)((value >> 8) & 0xFF);  /* MSB */
        buf->data[buf->position++] = (u8)( value       & 0xFF);  /* LSB */
    } else {
        /* Little-endian: low byte first */
        buf->data[buf->position++] = (u8)( value       & 0xFF);  /* LSB */
        buf->data[buf->position++] = (u8)((value >> 8) & 0xFF);  /* MSB */
    }
}

/*
 * buffer_write_int - Write 32-bit integer with specified byte order
 * 
 * @param buf    Buffer to write to
 * @param value  32-bit value
 * @param order  Byte order
 * 
 * BIT SHIFTING EXPLAINED:
 * 
 * value = 0x12345678
 * 
 * Byte 3 (MSB):  (value >> 24) & 0xFF = 0x12
 * Byte 2:        (value >> 16) & 0xFF = 0x34
 * Byte 1:        (value >>  8) & 0xFF = 0x56
 * Byte 0 (LSB):  (value >>  0) & 0xFF = 0x78
 * 
 * BIG-ENDIAN LAYOUT:
 * ┌──────┬──────┬──────┬──────┐
 * │ 0x12 │ 0x34 │ 0x56 │ 0x78 │  (MSB → LSB)
 * └──────┴──────┴──────┴──────┘
 *   [0]    [1]    [2]    [3]
 * 
 * LITTLE-ENDIAN LAYOUT:
 * ┌──────┬──────┬──────┬──────┐
 * │ 0x78 │ 0x56 │ 0x34 │ 0x12 │  (LSB → MSB)
 * └──────┴──────┴──────┴──────┘
 *   [0]    [1]    [2]    [3]
 * 
 * COMPLEXITY: O(1) amortized
 */
void buffer_write_int(StreamBuffer* buf, u32 value, ByteOrder order) {
    ensure_capacity(buf, 4);
    
    if (order == BYTE_ORDER_BIG) {
        /* Big-endian: MSB first */
        buf->data[buf->position++] = (u8)((value >> 24) & 0xFF);
        buf->data[buf->position++] = (u8)((value >> 16) & 0xFF);
        buf->data[buf->position++] = (u8)((value >>  8) & 0xFF);
        buf->data[buf->position++] = (u8)( value        & 0xFF);
    } else {
        /* Little-endian: LSB first */
        buf->data[buf->position++] = (u8)( value        & 0xFF);
        buf->data[buf->position++] = (u8)((value >>  8) & 0xFF);
        buf->data[buf->position++] = (u8)((value >> 16) & 0xFF);
        buf->data[buf->position++] = (u8)((value >> 24) & 0xFF);
    }
}

/*
 * buffer_write_long - Write 64-bit integer (always big-endian)
 * 
 * @param buf    Buffer to write to
 * @param value  64-bit value
 * 
 * NOTE: Always uses big-endian (RuneScape protocol standard)
 * 
 * ALGORITHM:
 *   Loop from byte 7 (MSB) down to byte 0 (LSB)
 *   Extract each byte using right shift and mask
 * 
 * EXAMPLE: value = 0x0123456789ABCDEF
 * 
 * Iteration | i | Shift  | Extracted Byte | Written To
 * ----------+---+--------+----------------+------------
 *     0     | 7 | 7*8=56 |     0x01       |   buf[pos]
 *     1     | 6 | 6*8=48 |     0x23       | buf[pos+1]
 *     2     | 5 | 5*8=40 |     0x45       | buf[pos+2]
 *     3     | 4 | 4*8=32 |     0x67       | buf[pos+3]
 *     4     | 3 | 3*8=24 |     0x89       | buf[pos+4]
 *     5     | 2 | 2*8=16 |     0xAB       | buf[pos+5]
 *     6     | 1 | 1*8=8  |     0xCD       | buf[pos+6]
 *     7     | 0 | 0*8=0  |     0xEF       | buf[pos+7]
 * 
 * COMPLEXITY: O(1) amortized (loop is constant 8 iterations)
 */
void buffer_write_long(StreamBuffer* buf, u64 value) {
    ensure_capacity(buf, 8);
    
    /* Write 8 bytes from MSB to LSB */
    for (int i = 7; i >= 0; --i) {
        buf->data[buf->position++] = (u8)((value >> (i * 8)) & 0xFF);
    }
}

/*
 * buffer_write_bytes - Write raw byte array (memcpy wrapper)
 * 
 * @param buf     Buffer to write to
 * @param data    Source byte array
 * @param length  Number of bytes to copy
 * 
 * USE CASE: Copying encrypted blocks, file data, etc.
 * 
 * COMPLEXITY: O(length) time
 */
void buffer_write_bytes(StreamBuffer* buf, const u8* data, u32 length) {
    if (!data || length == 0) return;
    
    ensure_capacity(buf, length);
    
    /* Fast memory copy */
    memcpy(buf->data + buf->position, data, length);
    buf->position += length;
}

/*
 * buffer_write_string - Write C string with newline terminator
 * 
 * @param buf   Buffer to write to
 * @param cstr  Null-terminated C string (or NULL for empty)
 * 
 * RUNESCAPE PROTOCOL:
 *   - Standard C strings end with '\0' (0x00)
 *   - RuneScape protocol uses '\n' (0x0A) instead
 *   - Allows strings to contain null bytes
 * 
 * EXAMPLE:
 *   Input:  "hello"
 *   Output: [0x68][0x65][0x6C][0x6C][0x6F][0x0A]
 *            'h'   'e'   'l'   'l'   'o'   '\n'
 * 
 * COMPLEXITY: O(strlen(cstr)) time
 */
void buffer_write_string(StreamBuffer* buf, const char* cstr) {
    if (!cstr) cstr = "";  /* Treat NULL as empty string */
    
    u32 len = (u32)strlen(cstr);
    ensure_capacity(buf, len + 1);  /* +1 for terminator */
    
    /* Copy string bytes */
    memcpy(buf->data + buf->position, cstr, len);
    buf->position += len;
    
    /* RuneScape protocol: newline terminator instead of null */
    buf->data[buf->position++] = 10;  /* '\n' = 0x0A */
}

/*******************************************************************************
 * BYTE-LEVEL READ OPERATIONS
 ******************************************************************************/

/*
 * buffer_read_byte - Read single byte (optionally decrypt with ISAAC)
 * 
 * @param buf    Buffer to read from
 * @param isaac  If true and cipher attached, decrypt byte
 * @return       Byte value (possibly decrypted)
 * 
 * ISAAC DECRYPTION:
 *   encrypted_byte = data[position]
 *   key_byte = isaac_get_next(cipher) & 0xFF
 *   decrypted_byte = (encrypted_byte - key_byte) & 0xFF
 * 
 * WHY SUBTRACTION?
 *   Encryption uses:   encrypted = (plain + key) & 0xFF
 *   Decryption uses:   plain = (encrypted - key) & 0xFF
 *   This is symmetric modular arithmetic (mod 256)
 * 
 * EXAMPLE:
 *   Encryption: plain=0x42, key=0x17
 *               encrypted = (0x42 + 0x17) & 0xFF = 0x59
 *   
 *   Decryption: encrypted=0x59, key=0x17
 *               plain = (0x59 - 0x17) & 0xFF = 0x42 ✓
 * 
 * COMPLEXITY: O(1) time
 */
u8 buffer_read_byte(StreamBuffer* buf, bool isaac) {
    /* Read raw byte */
    u8 value = buf->data[buf->position++];
    
    /* Decrypt if requested and cipher is initialized */
    if (isaac && buf->cipher && buf->cipher->initialized) {
        value = (u8)(value - (isaac_get_next(buf->cipher) & 0xFF));
    }
    
    return value;
}

/*
 * buffer_read_short - Read 16-bit integer with endianness conversion
 * 
 * @param buf        Buffer to read from
 * @param is_signed  If true, interpret as signed integer (two's complement)
 * @param order      Byte order
 * @return           16-bit value (possibly sign-extended)
 * 
 * SIGNED INTEGER HANDLING:
 * 
 * TWO'S COMPLEMENT REPRESENTATION:
 *   - MSB is sign bit (0=positive, 1=negative)
 *   - Positive: same as unsigned
 *   - Negative: complement all bits and add 1
 * 
 * EXAMPLE: -1 as 16-bit signed
 *   Binary:    1111 1111 1111 1111 = 0xFFFF
 *   Unsigned:  65535
 *   Signed:    -1
 * 
 * SIGN EXTENSION:
 *   If MSB (bit 15) is set, value is negative
 *   
 *   value = 0xFFFF
 *   Check: v & 0x8000 = 0xFFFF & 0x8000 = 0x8000 (non-zero, so negative)
 *   
 *   Convert to negative:
 *     1. Complement: ~v = ~0xFFFF = 0x0000
 *     2. Add 1:      (~v + 1) = 0x0001
 *     3. Mask:       (0x0001) & 0xFFFF = 0x0001
 *     4. Negate:     -(0x0001) = -1
 * 
 * COMPLEXITY: O(1) time
 */
u16 buffer_read_short(StreamBuffer* buf, bool is_signed, ByteOrder order) {
    u16 hi, lo;
    
    /* Read bytes in specified order */
    if (order == BYTE_ORDER_BIG) {
        hi = buf->data[buf->position++];  /* MSB first */
        lo = buf->data[buf->position++];
    } else {
        lo = buf->data[buf->position++];  /* LSB first */
        hi = buf->data[buf->position++];
    }
    
    /* Combine bytes into 16-bit value */
    u16 v = (u16)((hi << 8) | lo);
    
    /* Handle signed conversion if requested */
    if (is_signed && (v & 0x8000)) {
        /* Negative value: convert from two's complement */
        return (u16)(-(i16)((~v + 1) & 0xFFFF));
    }
    
    return v;
}

/*
 * buffer_read_int - Read 32-bit integer with endianness conversion
 * 
 * @param buf    Buffer to read from
 * @param order  Byte order
 * @return       32-bit value
 * 
 * BIT COMBINING:
 * 
 * Big-endian bytes: [b0=0x12][b1=0x34][b2=0x56][b3=0x78]
 * 
 * Combine using shifts and OR:
 *   result = (b0 << 24) | (b1 << 16) | (b2 << 8) | b3
 * 
 * Step-by-step:
 *   b0 << 24 = 0x12000000
 *   b1 << 16 = 0x00340000
 *   b2 <<  8 = 0x00005600
 *   b3 <<  0 = 0x00000078
 *   OR all:  = 0x12345678
 * 
 * COMPLEXITY: O(1) time
 */
u32 buffer_read_int(StreamBuffer* buf, ByteOrder order) {
    /* Read 4 bytes */
    u32 b0 = buf->data[buf->position++];
    u32 b1 = buf->data[buf->position++];
    u32 b2 = buf->data[buf->position++];
    u32 b3 = buf->data[buf->position++];
    
    if (order == BYTE_ORDER_BIG) {
        /* Big-endian: b0 is MSB */
        return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
    } else {
        /* Little-endian: b3 is MSB */
        return (b3 << 24) | (b2 << 16) | (b1 << 8) | b0;
    }
}

/*******************************************************************************
 * BIT-LEVEL ACCESS
 * 
 * CONCEPT: Bit packing compresses multiple small values into bytes
 * 
 * EXAMPLE: Player update encoding
 *   - Movement type: 2 bits (values 0-3)
 *   - Direction:     3 bits (values 0-7)
 *   - Has update:    1 bit (boolean)
 *   Total: 6 bits instead of 3 bytes (50% space savings)
 * 
 * BIT ADDRESSING SYSTEM:
 * 
 *   Byte Index:  0          1          2          3
 *   Bit Index:   76543210   76543210   76543210   76543210
 *   Global Bit:  0......7   8.....15  16.....23  24.....31
 * 
 * Example: Write 3 bits starting at bit 5
 * 
 * Before: Byte 0 = [????_????]  (bit_position = 5)
 *                       ↑
 *                   start here
 * 
 * Value to write: 0b101 (3 bits)
 * 
 * After:  Byte 0 = [????_?101]  (bit_position = 8)
 *                          ↑↑↑
 *                      written here
 * 
 ******************************************************************************/

/*
 * buffer_start_bit_access - Enter bit-level mode
 * 
 * EFFECTS: Synchronizes bit_position with current byte position
 * 
 * FORMULA: bit_position = position * 8
 * 
 * EXAMPLE:
 *   position = 3 (at byte 3)
 *   bit_position = 3 * 8 = 24 (at bit 24)
 * 
 * COMPLEXITY: O(1) time
 */
void buffer_start_bit_access(StreamBuffer* buf) {
    buf->bit_position = buf->position * 8;
}

/*
 * buffer_finish_bit_access - Return to byte-level mode
 * 
 * EFFECTS: Rounds bit_position up to next byte boundary
 * 
 * FORMULA: position = (bit_position + 7) / 8
 * 
 * WHY +7?
 *   Rounds up to nearest byte:
 *   - bit 0-7   → byte 0 + 1 = byte 1
 *   - bit 8-15  → byte 1 + 1 = byte 2
 *   - bit 16-23 → byte 2 + 1 = byte 3
 * 
 * EXAMPLES:
 *   bit_position = 0  → (0+7)/8  = 0
 *   bit_position = 1  → (1+7)/8  = 1
 *   bit_position = 8  → (8+7)/8  = 1
 *   bit_position = 9  → (9+7)/8  = 2
 *   bit_position = 15 → (15+7)/8 = 2
 *   bit_position = 16 → (16+7)/8 = 2
 * 
 * COMPLEXITY: O(1) time
 */
void buffer_finish_bit_access(StreamBuffer* buf) {
    buf->position = (buf->bit_position + 7) / 8;
}

/*
 * BIT_MASK - Precomputed bitmasks for fast masking operations
 * 
 * BIT_MASK[n] = (1 << n) - 1 = all 1's in lower n bits
 * 
 * Examples:
 *   BIT_MASK[0]  = 0b0          = 0x0
 *   BIT_MASK[1]  = 0b1          = 0x1
 *   BIT_MASK[2]  = 0b11         = 0x3
 *   BIT_MASK[3]  = 0b111        = 0x7
 *   BIT_MASK[4]  = 0b1111       = 0xF
 *   BIT_MASK[8]  = 0b11111111   = 0xFF
 *   BIT_MASK[16] = 0xFFFF
 *   BIT_MASK[32] = 0xFFFFFFFF
 * 
 * USE: Extract lower N bits:  value & BIT_MASK[n]
 */
static const u32 BIT_MASK[33] = {
    0,          /* 0 bits */
    0x1,        /* 1 bit  */
    0x3,        /* 2 bits */
    0x7,        /* 3 bits */
    0xf,        /* 4 bits */
    0x1f,       /* 5 bits */
    0x3f,       /* 6 bits */
    0x7f,       /* 7 bits */
    0xff,       /* 8 bits */
    0x1ff,      /* 9 bits */
    0x3ff,      /* 10 bits */
    0x7ff,      /* 11 bits */
    0xfff,      /* 12 bits */
    0x1fff,     /* 13 bits */
    0x3fff,     /* 14 bits */
    0x7fff,     /* 15 bits */
    0xffff,     /* 16 bits */
    0x1ffff,    /* 17 bits */
    0x3ffff,    /* 18 bits */
    0x7ffff,    /* 19 bits */
    0xfffff,    /* 20 bits */
    0x1fffff,   /* 21 bits */
    0x3fffff,   /* 22 bits */
    0x7fffff,   /* 23 bits */
    0xffffff,   /* 24 bits */
    0x1ffffff,  /* 25 bits */
    0x3ffffff,  /* 26 bits */
    0x7ffffff,  /* 27 bits */
    0xfffffff,  /* 28 bits */
    0x1fffffff, /* 29 bits */
    0x3fffffff, /* 30 bits */
    0x7fffffff, /* 31 bits */
    0xffffffff  /* 32 bits */
};

/*
 * buffer_write_bits - Write arbitrary-width bit field
 * 
 * @param buf       Buffer to write to
 * @param num_bits  Number of bits to write (1-32)
 * @param value     Value to encode (only lower num_bits are used)
 * 
 * ALGORITHM:
 *   This implements bit-level writing across byte boundaries
 *   
 *   Key concepts:
 *   1. byte_pos = which byte currently writing to
 *   2. bit_offset = how many bits left in current byte
 *   3. Write MSB-first (like big-endian for bits)
 * 
 * DETAILED EXAMPLE: Write 11 bits (value=0x5A3) at bit_position=13
 * 
 * Setup:
 *   value = 0x5A3 = 0b10110100011 (11 bits)
 *   bit_position = 13
 *   byte_pos = 13 >> 3 = 1  (writing to byte 1)
 *   bit_offset = 8 - (13 & 7) = 8 - 5 = 3  (3 bits left in byte 1)
 * 
 * Initial buffer state:
 *   Byte 0: [xxxx_xxxx]
 *   Byte 1: [xxxx_x???]  ← bit_position=13 points here (3 bits left)
 *                    ↑
 *               start here
 *   Byte 2: [????_????]
 *   Byte 3: [????_????]
 * 
 * Iteration 1: Write first 3 bits
 *   num_bits (11) > bit_offset (3), so split across bytes
 *   
 *   Extract top 3 bits: value >> (11-3) = 0x5A3 >> 8 = 0b101
 *   Mask to 3 bits: 0b101 & BIT_MASK[3] = 0b101
 *   
 *   Write to byte 1 (preserve upper 5 bits):
 *     Clear lower 3 bits:  byte[1] &= ~BIT_MASK[3] = byte[1] & 0xF8
 *     Set new bits:        byte[1] |= 0b101 = 0b00000101
 *   
 *   State after iteration 1:
 *     Byte 1: [xxxx_x101]  ← wrote 3 bits
 *     Remaining: 11 - 3 = 8 bits to write
 *     byte_pos++: now byte 2
 *     bit_offset = 8  (fresh byte)
 * 
 * Iteration 2: Write remaining 8 bits
 *   num_bits (8) == bit_offset (8), special case
 *   
 *   value & BIT_MASK[8] = 0xA3 & 0xFF = 0xA3 = 0b10100011
 *   
 *   Write entire byte:
 *     byte[2] = 0b10100011
 *   
 *   Final state:
 *     Byte 1: [xxxx_x101]  ← upper 3 bits of value
 *     Byte 2: [1010_0011]  ← lower 8 bits of value
 *     bit_position: 13 + 11 = 24
 * 
 * COMPLEXITY: O(num_bits) time (at most 5 byte writes for 32 bits)
 */
void buffer_write_bits(StreamBuffer* buf, u32 num_bits, u32 value) {
    /* Calculate byte and bit positions */
    u32 byte_pos   = buf->bit_position >> 3;        /* Divide by 8 */
    u32 bit_offset = 8 - (buf->bit_position & 7);   /* Modulo 8, then invert */
    
    /* Save for debug logging */
    u32 initial_bit_pos  = buf->bit_position;
    u32 initial_byte_pos = byte_pos;
    u32 orig_num_bits    = num_bits;
    
    /* Advance bit cursor */
    buf->bit_position += num_bits;
    
    /* Ensure buffer has enough bytes for final bit position */
    ensure_capacity(buf, ((buf->bit_position + 7) >> 3) - byte_pos);
    
    /* Write bits, potentially across multiple bytes */
    while (num_bits > bit_offset) {
        /* Case 1: Value spans current byte and next byte(s) */
        
        /* Clear lower bit_offset bits of current byte */
        buf->data[byte_pos] &= ~BIT_MASK[bit_offset];
        
        /* Extract upper bit_offset bits of value and write to current byte */
        buf->data[byte_pos] |= (u8)((value >> (num_bits - bit_offset)) & BIT_MASK[bit_offset]);
        
        byte_pos++;             /* Move to next byte */
        num_bits -= bit_offset; /* Reduce remaining bits */
        bit_offset = 8;         /* Next byte is fresh (8 bits available) */
    }
    
    if (num_bits == bit_offset) {
        /* Case 2: Remaining bits exactly fill current byte */
        buf->data[byte_pos] &= ~BIT_MASK[bit_offset];
        buf->data[byte_pos] |= (u8)(value & BIT_MASK[bit_offset]);
    } else {
        /* Case 3: Remaining bits fit within current byte (num_bits < bit_offset) */
        
        /* Calculate position of bits within byte */
        u32 shift = bit_offset - num_bits;
        
        /* Clear the bit range for writing */
        buf->data[byte_pos] &= ~(BIT_MASK[num_bits] << shift);
        
        /* Write bits at correct position */
        buf->data[byte_pos] |= (u8)((value & BIT_MASK[num_bits]) << shift);
    }
    
    /* Debug logging for player info packets (bits 24-70 are critical) */
    if (initial_bit_pos >= 24 && initial_bit_pos <= 70) {
        printf("DEBUG_BITS: write %u bits (value=%u) at bitpos=%u -> bytes[%u..%u]:",
               orig_num_bits, value, initial_bit_pos, initial_byte_pos, byte_pos);
        for (u32 i = initial_byte_pos; i <= byte_pos && i < 12; i++) {
            printf(" %02X", buf->data[i]);
        }
        printf("\n");
    }
}

/*
 * buffer_read_bits - Read arbitrary-width bit field
 * 
 * @param buf       Buffer to read from
 * @param num_bits  Number of bits to read (1-32)
 * @return          Extracted value
 * 
 * ALGORITHM: Inverse of buffer_write_bits
 *   Accumulate bits from bytes, shifting and ORing
 * 
 * EXAMPLE: Read 11 bits starting at bit 13
 * 
 * Initial state:
 *   Byte 1: [xxxx_x101]  bit_position=13 (3 bits available)
 *   Byte 2: [1010_0011]
 *   
 * Iteration 1: Read 3 bits from byte 1
 *   value = 0
 *   value <<= 3  (shift left by bit_offset)
 *   value |= byte[1] & 0b00000111  (mask lower 3 bits)
 *   value = 0b101
 *   num_bits = 11 - 3 = 8
 * 
 * Iteration 2: Read 8 bits from byte 2
 *   value <<= 8
 *   value = 0b10100000000
 *   value |= byte[2] & 0xFF
 *   value = 0b10110100011 = 0x5A3
 * 
 * COMPLEXITY: O(num_bits) time
 */
u32 buffer_read_bits(StreamBuffer* buf, u32 num_bits) {
    u32 byte_pos   = buf->bit_position >> 3;
    u32 bit_offset = 8 - (buf->bit_position & 7);
    u32 value = 0;
    
    buf->bit_position += num_bits;
    
    /* Read bits from bytes, accumulating in value */
    while (num_bits > bit_offset) {
        value <<= bit_offset;  /* Make room for new bits */
        value |= buf->data[byte_pos++] & (0xFF >> (8 - bit_offset));
        num_bits -= bit_offset;
        bit_offset = 8;
    }
    
    if (num_bits == bit_offset) {
        value <<= bit_offset;
        value |= buf->data[byte_pos] & (0xFF >> (8 - bit_offset));
    } else {
        value <<= num_bits;
        value |= (buf->data[byte_pos] >> (bit_offset - num_bits)) & (0xFF >> (8 - num_bits));
    }
    
    return value;
}

/*******************************************************************************
 * PACKET HEADER ENCODING
 ******************************************************************************/

/*
 * buffer_write_header - Write fixed-length packet header with ISAAC encryption
 * 
 * @param buf     Buffer to write to
 * @param opcode  Packet type (0-255)
 * @param cipher  ISAAC cipher for opcode obfuscation (or NULL for plaintext)
 * 
 * PACKET OPCODE OBFUSCATION:
 * 
 * Purpose: Prevent packet sniffing / reverse engineering
 * Method:  Add ISAAC keystream value to opcode
 * 
 * Encryption:
 *   key = isaac_get_next(cipher)  // Get next random value
 *   encrypted_opcode = (opcode + (key & 0xFF)) & 0xFF
 * 
 * Decryption (done by client):
 *   decrypted_opcode = (encrypted_opcode - (key & 0xFF)) & 0xFF
 * 
 * EXAMPLE:
 *   opcode = 184 (player info packet)
 *   key = 0x12345678
 *   key_byte = 0x78
 *   encrypted = (184 + 0x78) & 0xFF = (0xB8 + 0x78) & 0xFF = 0x30
 * 
 * WHY & 0xFF?
 *   Ensures result stays in byte range (0-255)
 *   Implements modular arithmetic mod 256
 * 
 * COMPLEXITY: O(1) time
 */
void buffer_write_header(StreamBuffer* buf, u8 opcode, ISAACCipher* cipher) {
    u8 original_opcode = opcode;
    
    /* Encrypt opcode if cipher is active */
    if (cipher && cipher->initialized) {
        u32 key = isaac_get_next(cipher);
        opcode = (u8)((opcode + (key & 0xFF)) & 0xFF);
        
        /* Debug logging for important packets */
        if (original_opcode == 237 || original_opcode == 184) {
            printf("DEBUG: Encrypted opcode %u -> %u (ISAAC key=%u)\n", 
                   original_opcode, opcode, key & 0xFF);
        }
    }
    
    buffer_write_byte(buf, opcode);
}

/*
 * buffer_write_header_var - Write variable-length packet header
 * 
 * @param buf     Buffer to write to
 * @param opcode  Packet type
 * @param cipher  ISAAC cipher (or NULL)
 * @param type    VAR_BYTE (1-byte length) or VAR_SHORT (2-byte length)
 * 
 * VARIABLE-LENGTH PACKET FORMAT:
 * 
 * VAR_BYTE (for payloads up to 255 bytes):
 * ┌──────────┬──────────┬─────────────────────┐
 * │  opcode  │  length  │      payload        │
 * │ (1 byte) │ (1 byte) │  (0-255 bytes)      │
 * └──────────┴──────────┴─────────────────────┘
 *     [0]        [1]         [2...N]
 * 
 * VAR_SHORT (for payloads up to 65535 bytes):
 * ┌──────────┬───────────────────┬─────────────────────┐
 * │  opcode  │      length       │      payload        │
 * │ (1 byte) │    (2 bytes)      │  (0-65535 bytes)    │
 * └──────────┴───────────────────┴─────────────────────┘
 *     [0]         [1] [2]              [3...N]
 * 
 * ALGORITHM:
 *   1. Write encrypted opcode
 *   2. Remember position of length field
 *   3. Write placeholder (0x00 bytes)
 *   4. Caller writes payload
 *   5. buffer_finish_var_header() backfills actual length
 * 
 * EXAMPLE WORKFLOW:
 *   StreamBuffer* buf = buffer_create(256);
 *   buffer_write_header_var(buf, 184, cipher, VAR_SHORT);  // pos=3
 *   buffer_write_int(buf, 12345, BIG);                     // pos=7
 *   buffer_write_short(buf, 999, BIG);                     // pos=9
 *   buffer_finish_var_header(buf, VAR_SHORT);              // backfill length=6
 * 
 * COMPLEXITY: O(1) time
 */
void buffer_write_header_var(StreamBuffer* buf, u8 opcode, ISAACCipher* cipher, VarHeaderType type) {
    /* Write encrypted opcode */
    buffer_write_header(buf, opcode, cipher);
    
    /* Remember where length field will be */
    buf->var_len_kind = type;
    
    if (type == VAR_BYTE) {
        /* 1-byte length field */
        buf->var_len_pos = buf->position;
        buffer_write_byte(buf, 0);  /* Placeholder (will be overwritten) */
    } else {
        /* VAR_SHORT: 2-byte length field (big-endian) */
        buf->var_len_pos = buf->position;
        buffer_write_short(buf, 0, BYTE_ORDER_BIG);  /* Placeholder */
    }
}

/*
 * buffer_finish_var_header - Backfill length field in variable-length packet
 * 
 * @param buf   Buffer to modify
 * @param type  Must match type from buffer_write_header_var()
 * 
 * ALGORITHM:
 *   1. Calculate payload length = current_position - header_end_position
 *   2. Overwrite length placeholder at var_len_pos
 *   3. Clear bookkeeping fields
 * 
 * LENGTH CALCULATION:
 * 
 * VAR_BYTE:
 *   Header size: 1 (opcode) + 1 (length) = 2 bytes
 *   Payload length = current_position - (var_len_pos + 1)
 * 
 * VAR_SHORT:
 *   Header size: 1 (opcode) + 2 (length) = 3 bytes
 *   Payload length = current_position - (var_len_pos + 2)
 * 
 * EXAMPLE:
 * 
 * Initial (after buffer_write_header_var with VAR_SHORT):
 * ┌────┬────┬────┬─────────────────┐
 * │0xXX│0x00│0x00│   (empty)       │  position=3, var_len_pos=1
 * └────┴────┴────┴─────────────────┘
 *  [0]  [1]  [2]
 *  opc  len  len
 * 
 * After writing payload (4 bytes):
 * ┌────┬────┬────┬────┬────┬────┬────┐
 * │0xXX│0x00│0x00│0xAA│0xBB│0xCC│0xDD│  position=7
 * └────┴────┴────┴────┴────┴────┴────┘
 *  [0]  [1]  [2]  [3]  [4]  [5]  [6]
 * 
 * Calculation:
 *   payload_len = 7 - (1 + 2) = 4
 *   hi_byte = (4 >> 8) & 0xFF = 0x00
 *   lo_byte = 4 & 0xFF = 0x04
 * 
 * After buffer_finish_var_header():
 * ┌────┬────┬────┬────┬────┬────┬────┐
 * │0xXX│0x00│0x04│0xAA│0xBB│0xCC│0xDD│
 * └────┴────┴────┴────┴────┴────┴────┘
 *        ↑────↑
 *     length=4
 * 
 * MISMATCH HANDLING:
 *   If var_len_kind doesn't match type parameter, assumes header at offset 0
 *   This is a safety fallback (shouldn't normally happen)
 * 
 * COMPLEXITY: O(1) time
 */
void buffer_finish_var_header(StreamBuffer* buf, VarHeaderType type) {
    /* Safety check: ensure header type matches */
    if (buf->var_len_kind != type) {
        /* Fallback: assume header starts at byte 0 */
        u32 end_pos = buf->position;
        
        if (type == VAR_BYTE) {
            u32 payload_len = (end_pos >= 2) ? (end_pos - 2) : 0;
            buf->data[1] = (u8)(payload_len & 0xFF);
        } else {
            u32 payload_len = (end_pos >= 3) ? (end_pos - 3) : 0;
            buf->data[1] = (u8)((payload_len >> 8) & 0xFF);
            buf->data[2] = (u8)( payload_len       & 0xFF);
        }
        
        buf->var_len_kind = 0;
        buf->var_len_pos  = 0;
        return;
    }
    
    const u32 end_pos = buf->position;
    
    if (type == VAR_BYTE) {
        /* Calculate payload length (excludes opcode and length byte) */
        const u32 payload_len = (end_pos > (buf->var_len_pos + 1))
            ? (end_pos - (buf->var_len_pos + 1))
            : 0;
        
        /* Overwrite placeholder at var_len_pos */
        buf->data[buf->var_len_pos] = (u8)(payload_len & 0xFF);
        
    } else { /* VAR_SHORT */
        /* Calculate payload length (excludes opcode and 2-byte length) */
        const u32 payload_len = (end_pos > (buf->var_len_pos + 2))
            ? (end_pos - (buf->var_len_pos + 2))
            : 0;
        
        /* Write 16-bit length in big-endian */
        const u8 hi = (u8)((payload_len >> 8) & 0xFF);
        const u8 lo = (u8)( payload_len       & 0xFF);
        
        buf->data[buf->var_len_pos + 0] = hi;
        buf->data[buf->var_len_pos + 1] = lo;
    }
    
    /* Clear bookkeeping (allows header reuse) */
    buf->var_len_kind = 0;
    buf->var_len_pos  = 0;
}
