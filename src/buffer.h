/*******************************************************************************
 * BUFFER.H - Dynamic Byte Stream with Bit-Level Access
 *******************************************************************************
 * 
 * EDUCATIONAL PURPOSE:
 * This file demonstrates fundamental concepts in:
 *   - Dynamic memory management (heap allocation, reallocation)
 *   - Byte-level I/O (reading/writing primitive types)
 *   - Bit manipulation (packing multiple values into bytes)
 *   - Endianness (byte order in multi-byte integers)
 *   - Network protocol design (packet framing, variable-length encoding)
 *   - Stream ciphers (ISAAC encryption integration)
 * 
 * CORE CONCEPT - STREAMBUFFER:
 * A StreamBuffer is a dynamically-resizing array of bytes that supports:
 *   1. Sequential read/write of multi-byte primitives (u8, u16, u32, u64)
 *   2. Bit-level access for compact encoding (e.g., 3-bit direction, 11-bit index)
 *   3. Byte order control (big-endian/little-endian)
 *   4. Optional encryption via ISAAC cipher
 *   5. Variable-length packet headers (1 or 2-byte length prefix)
 * 
 * MEMORY LAYOUT:
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │ StreamBuffer Structure (24 bytes on 32-bit, 40 bytes on 64-bit)         │
 * ├──────────────┬──────────────┬──────────────┬────────────────────────────┤
 * │ u8* data     │ u32 capacity │ u32 position │ u32 bit_position           │
 * │ (8 bytes)    │ (4 bytes)    │ (4 bytes)    │ (4 bytes)                  │
 * ├──────────────┴──────────────┴──────────────┴────────────────────────────┤
 * │ ISAACCipher* cipher (8 bytes)                                           │
 * ├─────────────────────────────────────────────────────────────────────────┤
 * │ u32 var_len_pos (4 bytes)   │ VarHeaderType var_len_kind (4 bytes)      │
 * └─────────────────────────────────────────────────────────────────────────┘
 * 
 * DATA BUFFER LAYOUT (heap-allocated):
 * ┌────┬────┬────┬────┬────┬────┬────┬────┬─────────────┬────┐
 * │ 0  │ 1  │ 2  │ 3  │ 4  │ 5  │ 6  │ 7  │     ...     │ N  │
 * └────┴────┴────┴────┴────┴────┴────┴────┴─────────────┴────┘
 *   ↑                         ↑                             ↑
 *   data                   position                     capacity
 * 
 * USAGE PATTERN:
 * 1. Create:  StreamBuffer* buf = buffer_create(256);
 * 2. Write:   buffer_write_byte(buf, 42);
 *             buffer_write_short(buf, 1000, BYTE_ORDER_BIG);
 * 3. Read:    buffer_set_position(buf, 0);
 *             u8 value = buffer_read_byte(buf, false);
 * 4. Destroy: buffer_destroy(buf);
 * 
 ******************************************************************************/

#ifndef BUFFER_H
#define BUFFER_H

#include <stdio.h>
#include <string.h>
#include "types.h"
#include "isaac.h"

/*******************************************************************************
 * STREAMBUFFER - Dynamic Byte Array with Bit-Level Access
 *******************************************************************************
 * 
 * FIELDS:
 *   data:         Pointer to heap-allocated byte array (dynamically resized)
 *   capacity:     Total allocated size in bytes
 *   position:     Current read/write cursor (byte offset)
 *   bit_position: Current bit offset (position * 8 + bit offset within byte)
 *   cipher:       Optional ISAAC cipher for encryption/decryption
 *   var_len_pos:  Position of length byte(s) for variable-length packets
 *   var_len_kind: Type of variable header (VAR_BYTE=1 byte, VAR_SHORT=2 bytes)
 * 
 * INVARIANTS:
 *   - 0 <= position <= capacity
 *   - bit_position = position * 8 (when in byte mode)
 *   - data != NULL after successful creation
 * 
 * COMPLEXITY:
 *   - Space: O(capacity) heap memory
 *   - Create: O(capacity) for initial allocation + zero initialization
 *   - Write:  O(1) amortized (O(n) when reallocation needed)
 *   - Read:   O(1)
 ******************************************************************************/
typedef struct StreamBuffer {
    u8*  data;            /* Heap-allocated byte array */
    u32  capacity;        /* Total bytes allocated */
    u32  position;        /* Current read/write offset (bytes) */
    u32  bit_position;    /* Current bit offset (for bit packing) */
    
    ISAACCipher* cipher;  /* Optional stream cipher for encryption */
    
    /* Variable-length packet bookkeeping */
    u32           var_len_pos;   /* Byte offset of length field */
    VarHeaderType var_len_kind;  /* VAR_BYTE (1) or VAR_SHORT (2), 0=none */
} StreamBuffer;

/*******************************************************************************
 * LIFECYCLE MANAGEMENT
 ******************************************************************************/

/*
 * buffer_create - Allocate and initialize a new StreamBuffer
 * 
 * @param capacity  Initial size in bytes (will grow if needed)
 * @return          Pointer to new buffer, or NULL on allocation failure
 * 
 * MEMORY:
 *   - Allocates sizeof(StreamBuffer) + capacity bytes on heap
 *   - Initializes all bytes to zero
 * 
 * COMPLEXITY: O(capacity) time, O(capacity) space
 */
StreamBuffer* buffer_create(u32 capacity);

/*
 * buffer_destroy - Free all memory associated with buffer
 * 
 * @param buf  Buffer to destroy (may be NULL)
 * 
 * SAFETY: Always sets internal pointers to NULL before freeing
 * COMPLEXITY: O(1) time
 */
void buffer_destroy(StreamBuffer* buf);

/*
 * buffer_reset - Reset read/write cursors to beginning
 * 
 * @param buf  Buffer to reset
 * 
 * EFFECTS:
 *   - position = 0
 *   - bit_position = 0
 *   - var_len_pos = 0
 *   - var_len_kind = 0
 *   - Does NOT clear data or free memory
 * 
 * COMPLEXITY: O(1) time
 */
void buffer_reset(StreamBuffer* buf);

/*******************************************************************************
 * CURSOR MANAGEMENT
 ******************************************************************************/

/*
 * buffer_get_position - Get current byte offset
 * 
 * @param buf  Buffer to query
 * @return     Current position, or 0 if buf is NULL
 * 
 * COMPLEXITY: O(1) time
 */
u32 buffer_get_position(const StreamBuffer* buf);

/*
 * buffer_set_position - Move read/write cursor to specific offset
 * 
 * @param buf  Buffer to modify
 * @param pos  New position (clamped to capacity if too large)
 * 
 * EFFECTS: Also updates bit_position = pos * 8
 * COMPLEXITY: O(1) time
 */
void buffer_set_position(StreamBuffer* buf, u32 pos);

/*
 * buffer_get_remaining - Get unread bytes from current position
 * 
 * @param buf  Buffer to query
 * @return     capacity - position, or 0 if buf is NULL
 * 
 * COMPLEXITY: O(1) time
 */
u32 buffer_get_remaining(const StreamBuffer* buf);

/*
 * buffer_skip - Advance position by N bytes (bounded by remaining)
 * 
 * @param buf    Buffer to modify
 * @param count  Bytes to skip (clamped to remaining if too large)
 * 
 * COMPLEXITY: O(1) time
 */
void buffer_skip(StreamBuffer* buf, u32 count);

/*******************************************************************************
 * CIPHER INTEGRATION
 ******************************************************************************/

/*
 * buffer_set_cipher - Attach ISAAC cipher for encryption/decryption
 * 
 * @param buf     Buffer to modify
 * @param cipher  Pointer to initialized ISAAC cipher (or NULL to disable)
 * 
 * EFFECTS: Future reads/writes may be encrypted depending on API usage
 * COMPLEXITY: O(1) time
 */
void buffer_set_cipher(StreamBuffer* buf, ISAACCipher* cipher);

/*******************************************************************************
 * BYTE-LEVEL WRITE OPERATIONS
 * 
 * All write functions automatically grow the buffer if needed.
 * Growth strategy: double capacity until space is available.
 ******************************************************************************/

/*
 * buffer_write_byte - Write single byte
 * 
 * @param buf    Buffer to write to
 * @param value  Byte value (0-255)
 * 
 * MEMORY: May trigger realloc (doubles capacity if full)
 * COMPLEXITY: O(1) amortized
 */
void buffer_write_byte(StreamBuffer* buf, u8 value);

/*
 * buffer_write_short - Write 16-bit integer
 * 
 * @param buf    Buffer to write to
 * @param value  16-bit value (0-65535)
 * @param order  Byte order (BYTE_ORDER_BIG or BYTE_ORDER_LITTLE)
 * 
 * BIG ENDIAN (network byte order):
 *   value = 0x1234  →  [0x12][0x34]  (MSB first)
 * 
 * LITTLE ENDIAN (x86 native):
 *   value = 0x1234  →  [0x34][0x12]  (LSB first)
 * 
 * COMPLEXITY: O(1) amortized
 */
void buffer_write_short(StreamBuffer* buf, u16 value, ByteOrder order);

/*
 * buffer_write_int - Write 32-bit integer
 * 
 * @param buf    Buffer to write to
 * @param value  32-bit value
 * @param order  Byte order
 * 
 * BIG ENDIAN:
 *   value = 0x12345678  →  [0x12][0x34][0x56][0x78]
 * 
 * COMPLEXITY: O(1) amortized
 */
void buffer_write_int(StreamBuffer* buf, u32 value, ByteOrder order);

/*
 * buffer_write_long - Write 64-bit integer (always big-endian)
 * 
 * @param buf    Buffer to write to
 * @param value  64-bit value
 * 
 * ENCODING: Always big-endian (MSB first)
 * COMPLEXITY: O(1) amortized
 */
void buffer_write_long(StreamBuffer* buf, u64 value);

/*
 * buffer_write_bytes - Write raw byte array
 * 
 * @param buf     Buffer to write to
 * @param data    Source byte array
 * @param length  Number of bytes to copy
 * 
 * COMPLEXITY: O(length) time
 */
void buffer_write_bytes(StreamBuffer* buf, const u8* data, u32 length);

/*
 * buffer_write_string - Write null-terminated C string as NL-terminated
 * 
 * @param buf   Buffer to write to
 * @param cstr  Null-terminated string (or NULL for empty)
 * 
 * ENCODING: RuneScape protocol uses '\n' (0x0A) instead of '\0'
 * EXAMPLE:  "hello"  →  [0x68][0x65][0x6C][0x6C][0x6F][0x0A]
 * 
 * COMPLEXITY: O(strlen(cstr)) time
 */
void buffer_write_string(StreamBuffer* buf, const char* cstr);

/*******************************************************************************
 * BYTE-LEVEL READ OPERATIONS
 ******************************************************************************/

/*
 * buffer_read_byte - Read single byte (optionally decrypted)
 * 
 * @param buf    Buffer to read from
 * @param isaac  If true and cipher is set, decrypt using ISAAC
 * @return       Byte value (possibly decrypted)
 * 
 * DECRYPTION: value = encrypted_value - (isaac_next() & 0xFF)
 * 
 * COMPLEXITY: O(1) time
 */
u8 buffer_read_byte(StreamBuffer* buf, bool isaac);

/*
 * buffer_read_short - Read 16-bit integer
 * 
 * @param buf        Buffer to read from
 * @param is_signed  If true, interpret as signed (two's complement)
 * @param order      Byte order
 * @return           16-bit value (possibly sign-extended)
 * 
 * COMPLEXITY: O(1) time
 */
u16 buffer_read_short(StreamBuffer* buf, bool is_signed, ByteOrder order);

/*
 * buffer_read_int - Read 32-bit integer
 * 
 * @param buf    Buffer to read from
 * @param order  Byte order
 * @return       32-bit value
 * 
 * COMPLEXITY: O(1) time
 */
u32 buffer_read_int(StreamBuffer* buf, ByteOrder order);

/*******************************************************************************
 * BIT-LEVEL ACCESS
 * 
 * CONCEPT: Some protocols pack multiple small values into bytes to save space.
 * Example: 3-bit direction (0-7) + 11-bit player index (0-2047) = 14 bits total
 * 
 * BIT ADDRESSING:
 *   Byte 0: bits [0..7]
 *   Byte 1: bits [8..15]
 *   Byte 2: bits [16..23]
 *   etc.
 * 
 * WORKFLOW:
 *   1. buffer_start_bit_access(buf)   - Enter bit mode
 *   2. buffer_write_bits(buf, 3, 5)   - Write 3 bits with value 5
 *   3. buffer_write_bits(buf, 11, 42) - Write 11 bits with value 42
 *   4. buffer_finish_bit_access(buf)  - Return to byte mode (rounds up position)
 ******************************************************************************/

/*
 * buffer_start_bit_access - Switch to bit-level mode
 * 
 * @param buf  Buffer to modify
 * 
 * EFFECTS: bit_position = position * 8
 * COMPLEXITY: O(1) time
 */
void buffer_start_bit_access(StreamBuffer* buf);

/*
 * buffer_finish_bit_access - Return to byte-level mode
 * 
 * @param buf  Buffer to modify
 * 
 * EFFECTS: position = (bit_position + 7) / 8  (rounds up to next byte)
 * COMPLEXITY: O(1) time
 */
void buffer_finish_bit_access(StreamBuffer* buf);

/*
 * buffer_write_bits - Write value as N-bit integer
 * 
 * @param buf       Buffer to write to
 * @param num_bits  Number of bits to write (1-32)
 * @param value     Value to encode (lower num_bits used)
 * 
 * EXAMPLE: Writing 14 bits with value 0x2A5B
 *   Input:  num_bits=14, value=0x2A5B (binary: 10 1010 0101 1011)
 *   Uses:   Lower 14 bits → 10 1010 0101 10 (0x2A5A after masking)
 * 
 * BIT LAYOUT (writing to bytes):
 *   ┌────────┬────────┬────────┐
 *   │76543210│76543210│76543210│  Bit indices within each byte
 *   └────────┴────────┴────────┘
 *   Write proceeds left-to-right, MSB first
 * 
 * COMPLEXITY: O(num_bits) time
 */
void buffer_write_bits(StreamBuffer* buf, u32 num_bits, u32 value);

/*
 * buffer_read_bits - Read N-bit integer
 * 
 * @param buf       Buffer to read from
 * @param num_bits  Number of bits to read (1-32)
 * @return          Value extracted from bit stream
 * 
 * COMPLEXITY: O(num_bits) time
 */
u32 buffer_read_bits(StreamBuffer* buf, u32 num_bits);

/*******************************************************************************
 * PACKET HEADER ENCODING
 * 
 * RuneScape uses three packet formats:
 * 
 * 1. FIXED-LENGTH:
 *    ┌────────┬──────────────┐
 *    │ opcode │   payload    │
 *    │(1 byte)│(known length)│
 *    └────────┴──────────────┘
 * 
 * 2. VAR-BYTE (up to 255 bytes payload):
 *    ┌────────┬────────┬─────────────────┐
 *    │ opcode │ length │    payload      │
 *    │(1 byte)│(1 byte)│(0-255 bytes)    │
 *    └────────┴────────┴─────────────────┘
 * 
 * 3. VAR-SHORT (up to 65535 bytes payload):
 *    ┌────────┬──────────────┬─────────────────┐
 *    │ opcode │    length    │    payload      │
 *    │(1 byte)│  (2 bytes)   │(0-65535 bytes)  │
 *    └────────┴──────────────┴─────────────────┘
 * 
 * OPCODE ENCRYPTION:
 *   encrypted_opcode = (opcode + isaac_next()) & 0xFF
 ******************************************************************************/

/*
 * buffer_write_header - Write fixed-length packet header
 * 
 * @param buf     Buffer to write to
 * @param opcode  Packet type identifier (0-255)
 * @param cipher  ISAAC cipher for opcode encryption (or NULL)
 * 
 * ENCODING: [encrypted_opcode]
 * COMPLEXITY: O(1) time
 */
void buffer_write_header(StreamBuffer* buf, u8 opcode, ISAACCipher* cipher);

/*
 * buffer_write_header_var - Write variable-length packet header
 * 
 * @param buf     Buffer to write to
 * @param opcode  Packet type identifier
 * @param cipher  ISAAC cipher for opcode encryption (or NULL)
 * @param type    VAR_BYTE (1-byte length) or VAR_SHORT (2-byte length)
 * 
 * ENCODING:
 *   VAR_BYTE:  [encrypted_opcode][0x00 placeholder]
 *   VAR_SHORT: [encrypted_opcode][0x00][0x00 placeholder]
 * 
 * USAGE: Call buffer_finish_var_header() after writing payload to backfill length
 * 
 * COMPLEXITY: O(1) time
 */
void buffer_write_header_var(StreamBuffer* buf, u8 opcode, ISAACCipher* cipher, VarHeaderType type);

/*
 * buffer_finish_var_header - Backfill length field in variable-length packet
 * 
 * @param buf   Buffer to modify
 * @param type  Must match type passed to buffer_write_header_var()
 * 
 * ALGORITHM:
 *   1. payload_length = current_position - (header_position + header_size)
 *   2. Overwrite length byte(s) at var_len_pos with payload_length
 * 
 * EXAMPLE:
 *   buffer_write_header_var(buf, 184, cipher, VAR_SHORT);  // pos=3 after this
 *   buffer_write_int(buf, 12345, BIG);                     // pos=7 now
 *   buffer_finish_var_header(buf, VAR_SHORT);              // writes 4 into bytes[1-2]
 * 
 * COMPLEXITY: O(1) time
 */
void buffer_finish_var_header(StreamBuffer* buf, VarHeaderType type);

/*******************************************************************************
 * DEBUG UTILITIES
 ******************************************************************************/

/*
 * dbg_log_send - Print packet send event to console
 * 
 * @param tag          Human-readable packet name
 * @param opcode       Packet opcode number
 * @param hdr          Header type ("fixed", "varbyte", "varshort")
 * @param payload_len  Size of payload in bytes
 * @param isaac_on     1 if ISAAC encryption enabled, 0 otherwise
 * 
 * OUTPUT FORMAT:
 *   [SEND] PLAYER_INFO op=184 hdr=varshort len=42 isaac=on
 * 
 * COMPLEXITY: O(1) time
 */
static inline void dbg_log_send(const char* tag, int opcode, const char* hdr, int payload_len, int isaac_on) {
    printf("[SEND] %s op=%d hdr=%s len=%d isaac=%s\n",
           tag, opcode, hdr, payload_len, isaac_on ? "on" : "off");
}

#endif /* BUFFER_H */
