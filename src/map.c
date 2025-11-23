/*******************************************************************************
 * MAP.C - RuneScape Map Region Management Implementation
 *******************************************************************************
 *
 * This file implements the map loading and streaming system for the RuneScape
 * server. It handles CRC32 calculation, region management, and chunked file
 * transmission to clients.
 *
 * KEY ALGORITHMS:
 *
 *   1. CRC32 CHECKSUM (IEEE 802.3)
 *   2. REGION DETERMINATION (3x3 grid)
 *   3. FILE CHUNKING (1000-byte segments)
 *   4. DUPLICATE ELIMINATION (unique file list)
 *
 * IMPLEMENTATION DETAILS:
 *
 *   CRC32 Implementation:
 *     - Table-driven for O(1) per-byte lookup
 *     - Lazy initialization (builds table on first use)
 *     - Standard polynomial: 0xEDB88320
 *     - Used by: ZIP, PNG, Ethernet, MPEG-2
 *
 *   Region Management:
 *     - Maintains 3x3 grid around player
 *     - Eliminates duplicate file coordinates
 *     - Handles boundary conditions gracefully
 *
 *   Chunked Transmission:
 *     - Prevents socket buffer overflow
 *     - Allows progress tracking
 *     - Maintains file integrity with offset tracking
 *
 * MEMORY MANAGEMENT:
 *
 *   Static allocations:
 *     - CRC32 table: 256 entries x 4 bytes = 1024 bytes
 *     - File coordinate list: 9 entries x 8 bytes = 72 bytes (stack)
 *
 *   Dynamic allocations:
 *     - Map file buffers: allocated per-file, freed after transmission
 *     - Packet buffers: allocated per-packet, freed after send
 *
 * CROSS-PLATFORM COMPATIBILITY:
 *
 *   Path separators:
 *     - Windows: backslash (\)
 *     - Unix/Linux: forward slash (/)
 *     - Handled via PATH_SEPARATOR macro
 *
 *******************************************************************************
 */

#include "map.h"
#include "buffer.h"
#include "packets.h"
#include "network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define PATH_SEPARATOR "\\"
#else
#include <sys/stat.h>
#define PATH_SEPARATOR "/"
#endif

/*
 *******************************************************************************
 * CRC32 CHECKSUM IMPLEMENTATION
 *******************************************************************************
 *
 * The CRC32 (Cyclic Redundancy Check) algorithm detects accidental changes to
 * raw data. It's based on polynomial division in the Galois field GF(2).
 *
 * MATHEMATICAL FOUNDATION:
 *
 *   The algorithm treats data as a polynomial with binary coefficients.
 *   For example, the byte 0b10110010 represents:
 *     x^7 + x^5 + x^4 + x^1
 *
 *   This polynomial is divided by the generator polynomial:
 *     0xEDB88320 (reversed form of 0x04C11DB7)
 *
 *   The remainder of this division is the CRC32 value.
 *
 * TABLE GENERATION:
 *
 *   The lookup table precomputes CRC values for all possible byte values
 *   (0-255). Each entry represents the CRC of that byte value.
 *
 *   For each byte value i:
 *     1. Start with crc = i
 *     2. For each of 8 bits:
 *        - If lowest bit is 1: shift right and XOR with polynomial
 *        - If lowest bit is 0: just shift right
 *     3. Store result in table[i]
 *
 *   Example for byte 0x00:
 *     Initial: 0x00000000
 *     After 8 shifts: 0x00000000
 *     table[0] = 0x00000000
 *
 *   Example for byte 0x01:
 *     Initial: 0x00000001
 *     Bit 0 is 1: (0x00000001 >> 1) ^ 0xEDB88320 = 0xF6798990
 *     (remaining bits are 0, so just shift)
 *     table[1] = 0x77073096
 *
 * TABLE LOOKUP ALGORITHM:
 *
 *   Once the table is built, calculating CRC for a message is fast:
 *
 *   1. Initialize CRC to 0xFFFFFFFF (all bits set)
 *   2. For each byte in message:
 *      a. XOR byte with low 8 bits of CRC
 *      b. Use result as index into table
 *      c. Shift CRC right 8 bits
 *      d. XOR with table entry
 *   3. Invert final CRC (XOR with 0xFFFFFFFF)
 *
 *   Visual representation of one iteration:
 *
 *     Current CRC: [--------32 bits--------]
 *                  [High 24]|[Low 8]
 *                            |
 *                            v
 *                      XOR with byte
 *                            |
 *                            v
 *                     table[result] ---
 *                                      |
 *     CRC >> 8:    [High 24]|[0x00]    |
 *                            |         |
 *                            v         v
 *     New CRC:           [XOR result]
 *
 * COMPLEXITY ANALYSIS:
 *
 *   Table initialization: O(256 * 8) = O(1) constant time
 *   CRC calculation: O(n) where n = message length in bytes
 *   Space: O(1) - fixed 256-entry table
 *
 * ERROR DETECTION CAPABILITY:
 *
 *   CRC32 can detect:
 *     - All single-bit errors
 *     - All double-bit errors
 *     - All errors with odd number of bits
 *     - All burst errors up to 32 bits
 *     - Most larger errors (99.9999% detection rate)
 *
 */

/* CRC32 lookup table (1024 bytes) */
static u32 crc32_table[256];
static bool crc32_initialized = false;

/*
 * FUNCTION: init_crc32_table (internal)
 *
 * Builds the CRC32 lookup table using the IEEE 802.3 polynomial.
 * This is called once on first CRC calculation.
 *
 * ALGORITHM:
 *   For i from 0 to 255:
 *     crc = i
 *     For j from 0 to 7:
 *       if (crc & 1):  // lowest bit is 1
 *         crc = (crc >> 1) XOR 0xEDB88320
 *       else:          // lowest bit is 0
 *         crc = crc >> 1
 *     table[i] = crc
 *
 * EXAMPLE TRACE (i=1):
 *   Start: crc = 0x00000001
 *   j=0: bit 0 is 1, crc = 0x00000000 ^ 0xEDB88320 = 0xF6798990
 *   j=1: bit 0 is 0, crc = 0x7B3CC4C8
 *   j=2: bit 0 is 0, crc = 0x3D9E6264
 *   j=3: bit 0 is 0, crc = 0x1ECF3132
 *   j=4: bit 0 is 0, crc = 0x0F679899
 *   j=5: bit 0 is 1, crc = 0x07B3CC4C ^ 0xEDB88320 = 0xEA65586C
 *   j=6: bit 0 is 0, crc = 0x7532AC36
 *   j=7: bit 0 is 0, crc = 0x3A99561B
 *   (Note: actual calculation may differ due to bit manipulation)
 */
static void init_crc32_table() {
    if (crc32_initialized) return;
    
    for (u32 i = 0; i < 256; i++) {
        u32 crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xedb88320u;
            } else {
                crc >>= 1;
            }
        }
        crc32_table[i] = crc;
    }
    crc32_initialized = true;
}

/*
 * FUNCTION: map_calculate_crc32
 *
 * Calculates CRC32 checksum for a data buffer.
 *
 * ALGORITHM WALKTHROUGH (example with 3 bytes: [0xAB, 0xCD, 0xEF]):
 *
 *   Initialization:
 *     crc = 0xFFFFFFFF
 *
 *   Byte 0 (0xAB):
 *     index = (0xFFFFFFFF ^ 0xAB) & 0xFF = 0x54
 *     crc = (0xFFFFFFFF >> 8) ^ table[0x54]
 *     crc = 0x00FFFFFF ^ table[0x54]
 *
 *   Byte 1 (0xCD):
 *     index = (crc ^ 0xCD) & 0xFF
 *     crc = (crc >> 8) ^ table[index]
 *
 *   Byte 2 (0xEF):
 *     index = (crc ^ 0xEF) & 0xFF
 *     crc = (crc >> 8) ^ table[index]
 *
 *   Finalization:
 *     result = ~crc (bitwise NOT)
 *
 * WHY INITIALIZE TO 0xFFFFFFFF?
 *   - Prevents leading zeros from being ignored
 *   - Ensures different CRC for messages that differ only in leading zeros
 *   - Example: [0x00, 0x01] vs [0x01] would have same CRC without this
 *
 * WHY INVERT FINAL VALUE?
 *   - Prevents trailing zeros from being ignored
 *   - Ensures different CRC for messages that differ only in trailing zeros
 *
 * PARAMETERS:
 *   data   - Buffer to checksum
 *   length - Number of bytes in buffer
 *
 * RETURNS:
 *   32-bit CRC32 checksum
 *
 * PERFORMANCE:
 *   ~3-4 CPU cycles per byte on modern processors
 *   Example: 5KB file = approximately 20,000 cycles = 5 microseconds at 4GHz
 */
u32 map_calculate_crc32(const u8* data, u32 length) {
    init_crc32_table();
    
    u32 crc = 0xffffffffu;
    for (u32 i = 0; i < length; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xff];
    }
    return ~crc;
}

/*
 * FUNCTION: map_get_file_coord
 *
 * Converts absolute world coordinate to file (region) coordinate.
 *
 * BIT SHIFT DIVISION:
 *   The operation (abs_coord >> 6) is equivalent to (abs_coord / 64)
 *   but much faster on all processors.
 *
 *   Why this works:
 *     - In binary, each position represents a power of 2
 *     - Shifting right by N positions divides by 2^N
 *     - 2^6 = 64, so >> 6 divides by 64
 *
 *   Binary example:
 *     3200 decimal = 0000110010000000 binary
 *     >> 6         = 0000000000110010 binary = 50 decimal
 *
 *   Performance comparison:
 *     Division (3200 / 64):  ~10-20 cycles on modern CPU
 *     Bit shift (3200 >> 6): ~1 cycle on modern CPU
 *
 * EDGE CASES:
 *   Coordinate 0:    0 >> 6 = 0 (first region)
 *   Coordinate 63:   63 >> 6 = 0 (still first region)
 *   Coordinate 64:   64 >> 6 = 1 (second region starts)
 *   Coordinate 127:  127 >> 6 = 1 (still second region)
 *   Coordinate 128:  128 >> 6 = 2 (third region starts)
 *
 * PARAMETERS:
 *   abs_coord - Absolute tile coordinate (typically 0-16383)
 *
 * RETURNS:
 *   File coordinate (typically 0-255)
 */
i32 map_get_file_coord(i32 abs_coord) {
    return abs_coord >> 6;
}

/*
 *******************************************************************************
 * HELPER STRUCTURES AND FUNCTIONS
 *******************************************************************************
 */

/*
 * STRUCTURE: FileCoord
 *
 * Represents a map file coordinate pair (x, z).
 * Used to track unique region files in the 3x3 grid.
 *
 * FIELDS:
 *   x - File X coordinate (0-255)
 *   z - File Z coordinate (0-255)
 *
 * MEMORY LAYOUT:
 *   +---+---+---+---+---+---+---+---+
 *   | x (4 bytes)   | z (4 bytes)   |
 *   +---+---+---+---+---+---+---+---+
 *   Total: 8 bytes per entry
 */
typedef struct {
    i32 x;
    i32 z;
} FileCoord;

/*
 * FUNCTION: add_unique (internal)
 *
 * Adds a file coordinate to the list only if it's not already present.
 * This prevents duplicate file transmissions when regions overlap at boundaries.
 *
 * WHY DUPLICATES CAN OCCUR:
 *   When a player is near a region boundary, the +/- 52 tile offsets
 *   can result in the same file coordinate being calculated multiple times.
 *
 *   Example at boundary (player at x=3200):
 *     Center calculation: 3200 >> 6 = 50
 *     East calculation: (3200 + 52) >> 6 = 3252 >> 6 = 50
 *     Result: Same file coordinate (50) appears twice
 *
 * ALGORITHM:
 *   1. Linear search through existing entries
 *   2. If (x, z) already exists, return immediately
 *   3. If not found, add to end of list and increment count
 *
 * COMPLEXITY:
 *   Time: O(n) where n = current file count (max 9)
 *   Space: O(1)
 *   Note: Linear search is fine for small N (9 max)
 *
 * PARAMETERS:
 *   files      - Array of file coordinates
 *   file_count - Pointer to current count (modified if new entry added)
 *   fx         - File X coordinate to add
 *   fz         - File Z coordinate to add
 */
static void add_unique(FileCoord* files, i32* file_count, i32 fx, i32 fz) {
    for (i32 i = 0; i < *file_count; i++) {
        if (files[i].x == fx && files[i].z == fz) {
            return;
        }
    }
    files[*file_count].x = fx;
    files[*file_count].z = fz;
    (*file_count)++;
}

/*
 * FUNCTION: read_file_to_buffer (internal)
 *
 * Reads an entire file into a dynamically allocated buffer.
 *
 * PROCESS:
 *   1. Open file in binary read mode
 *   2. Seek to end to determine file size
 *   3. Seek back to start
 *   4. Allocate buffer of exact size
 *   5. Read entire file in one operation
 *   6. Close file
 *
 * ERROR HANDLING:
 *   - File doesn't exist: return false
 *   - File is empty (size <= 0): return false
 *   - Memory allocation fails: return false
 *   - Read fails or incomplete: free buffer and return false
 *
 * MEMORY RESPONSIBILITY:
 *   Caller must free the allocated buffer when done.
 *
 * PARAMETERS:
 *   path - File path to read
 *   data - Output pointer to allocated buffer (set by function)
 *   size - Output file size (set by function)
 *
 * RETURNS:
 *   true if successful, false on any error
 *
 * EXAMPLE USAGE:
 *   u8* buffer = NULL;
 *   u32 size = 0;
 *   if (read_file_to_buffer("data/maps/m_50_50", &buffer, &size)) {
 *       // Use buffer...
 *       free(buffer);
 *   }
 */
static bool read_file_to_buffer(const char* path, u8** data, u32* size) {
    FILE* file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size <= 0) {
        fclose(file);
        return false;
    }
    
    *data = malloc(file_size);
    if (!*data) {
        fclose(file);
        return false;
    }
    
    size_t read_size = fread(*data, 1, file_size, file);
    fclose(file);
    
    if (read_size != file_size) {
        free(*data);
        return false;
    }
    
    *size = (u32)file_size;
    return true;
}

/*
 *******************************************************************************
 * MAP REGION LOADING FUNCTIONS
 *******************************************************************************
 */

/*
 * FUNCTION: map_send_load_area
 *
 * Sends LOAD_AREA packet with CRC checksums for 3x3 grid of map regions.
 *
 * REGION CALCULATION ALGORITHM:
 *
 *   Step 1: Determine center file coordinates
 *     centre_file_x = abs_x >> 6
 *     centre_file_z = abs_z >> 6
 *
 *   Step 2: Calculate surrounding file coordinates
 *     The magic number 52 ensures coverage:
 *       - 64 tiles per region
 *       - Player can see ~15 tiles in each direction
 *       - 52 > 64/2, ensuring adjacent regions are included
 *
 *     Cardinal directions (4 files):
 *       North: (centre_x, (abs_z + 52) >> 6)
 *       South: (centre_x, (abs_z - 52) >> 6)
 *       East:  ((abs_x + 52) >> 6, centre_z)
 *       West:  ((abs_x - 52) >> 6, centre_z)
 *
 *     Diagonal directions (4 files):
 *       NE: ((abs_x + 52) >> 6, (abs_z + 52) >> 6)
 *       NW: ((abs_x - 52) >> 6, (abs_z + 52) >> 6)
 *       SE: ((abs_x + 52) >> 6, (abs_z - 52) >> 6)
 *       SW: ((abs_x - 52) >> 6, (abs_z - 52) >> 6)
 *
 *   Step 3: Eliminate duplicates
 *     Use add_unique() to prevent sending same file twice
 *
 *   Step 4: Calculate CRCs for each unique file
 *     For each file coordinate (x, z):
 *       - Read m_X_Z (land file)
 *       - Calculate CRC32
 *       - Read l_X_Z (loc file)  
 *       - Calculate CRC32
 *       - Store both CRCs
 *
 *   Step 5: Build and send packet
 *     Write region_x, region_y
 *     For each file:
 *       Write file_x, file_z, land_crc, loc_crc
 *
 * PACKET SIZE CALCULATION:
 *
 *   Header: 3 bytes (opcode + length)
 *   Region coords: 4 bytes (2 shorts)
 *   Per file: 10 bytes (1 + 1 + 4 + 4)
 *   Max files: 9
 *   Total: 3 + 4 + (9 * 10) = 97 bytes maximum
 *
 * EXAMPLE SCENARIO:
 *
 *   Player at (3200, 3200):
 *     centre_file = (50, 50)
 *
 *   File calculations:
 *     Center: (50, 50)
 *     North:  (50, (3200+52)>>6) = (50, 50) [duplicate]
 *     South:  (50, (3200-52)>>6) = (50, 49)
 *     East:   ((3200+52)>>6, 50) = (50, 50) [duplicate]
 *     West:   ((3200-52)>>6, 50) = (49, 50)
 *     NE:     (50, 50) [duplicate]
 *     NW:     (49, 50) [duplicate]
 *     SE:     (50, 49) [duplicate]
 *     SW:     (49, 49)
 *
 *   Unique files: (50,50), (50,49), (49,50), (49,49) = 4 files
 *   Packet size: 3 + 4 + (4 * 10) = 47 bytes
 *
 * PARAMETERS:
 *   player   - Player to send to
 *   region_x - Region X for client's coordinate system
 *   region_y - Region Y for client's coordinate system
 */
void map_send_load_area(Player* player, i32 region_x, i32 region_y) {
    if (!player || player->socket_fd < 0) return;
    
    i32 abs_x = (i32)player->position.x;
    i32 abs_z = (i32)player->position.z;
    
    i32 centre_file_x = map_get_file_coord(abs_x);
    i32 centre_file_z = map_get_file_coord(abs_z);
    
    /* Collect unique map files to send (center + 8 surrounding) */
    FileCoord files[9];
    i32 file_count = 0;
    
    /* Center */
    add_unique(files, &file_count, centre_file_x, centre_file_z);
    
    /* Cardinal directions */
    add_unique(files, &file_count, centre_file_x, map_get_file_coord(abs_z + 52));
    add_unique(files, &file_count, centre_file_x, map_get_file_coord(abs_z - 52));
    add_unique(files, &file_count, map_get_file_coord(abs_x + 52), centre_file_z);
    add_unique(files, &file_count, map_get_file_coord(abs_x - 52), centre_file_z);
    
    /* Diagonals */
    add_unique(files, &file_count, map_get_file_coord(abs_x + 52), map_get_file_coord(abs_z + 52));
    add_unique(files, &file_count, map_get_file_coord(abs_x - 52), map_get_file_coord(abs_z + 52));
    add_unique(files, &file_count, map_get_file_coord(abs_x + 52), map_get_file_coord(abs_z - 52));
    add_unique(files, &file_count, map_get_file_coord(abs_x - 52), map_get_file_coord(abs_z - 52));
    
    /* Create packet */
    StreamBuffer* out = buffer_create(6 + file_count * 10);
    buffer_write_header_var(out, SERVER_LOAD_AREA, player->out_cipher.initialized ? &player->out_cipher : NULL, VAR_SHORT);
    u32 payload_start = out->position;

    i32 zone_x = abs_x >> 3;
    i32 zone_z = abs_z >> 3;
    buffer_write_short(out, zone_x, BYTE_ORDER_BIG);
    buffer_write_short(out, zone_z, BYTE_ORDER_BIG);
    
    /* Update player's origin for coordinate system tracking */
    player->origin_x = abs_x;
    player->origin_z = abs_z;
    
    /* Write file entries with CRCs */
    for (i32 i = 0; i < file_count; i++) {
        char land_path[256];
        char loc_path[256];
        u32 land_crc = 0;
        u32 loc_crc = 0;
        
        snprintf(land_path, sizeof(land_path), "data%smaps%sm%d_%d", 
                 PATH_SEPARATOR, PATH_SEPARATOR, files[i].x, files[i].z);
        snprintf(loc_path, sizeof(loc_path), "data%smaps%sl%d_%d", 
                 PATH_SEPARATOR, PATH_SEPARATOR, files[i].x, files[i].z);
        
        /* Calculate CRCs */
        u8* data = NULL;
        u32 size = 0;
        if (read_file_to_buffer(land_path, &data, &size)) {
            land_crc = map_calculate_crc32(data, size);
            free(data);
        }
        
        data = NULL;
        size = 0;
        if (read_file_to_buffer(loc_path, &data, &size)) {
            loc_crc = map_calculate_crc32(data, size);
            free(data);
        }
        
        buffer_write_byte(out, files[i].x);
        buffer_write_byte(out, files[i].z);
        buffer_write_int(out, (i32)land_crc, BYTE_ORDER_BIG);
        buffer_write_int(out, (i32)loc_crc, BYTE_ORDER_BIG);
    }
    
    buffer_finish_var_header(out, VAR_SHORT);
    dbg_log_send("LOAD_AREA", SERVER_LOAD_AREA, "varshort",
        (int)(out->position - payload_start),
          player->out_cipher.initialized ? 1 : 0);

    network_send(player->socket_fd, out->data, out->position);
    buffer_destroy(out);
    
    printf("Sent LOAD_AREA: region (%d, %d) with %d map files\n", region_x, region_y, file_count);
}

/*
 * FUNCTION: map_handle_request
 *
 * Processes client's request for specific map files.
 *
 * PACKET PARSING:
 *
 *   Packet structure (repeating 3-byte entries):
 *   +------+------+------+------+------+------+
 *   | Type | X    | Z    | Type | X    | Z    | ...
 *   | (1B) | (1B) | (1B) | (1B) | (1B) | (1B) |
 *   +------+------+------+------+------+------+
 *
 *   Number of entries = packet_length / 3
 *
 *   Example packet (6 bytes):
 *     [0, 50, 50, 1, 50, 50]
 *     Entry 1: type=0 (land), x=50, z=50
 *     Entry 2: type=1 (loc), x=50, z=50
 *
 * PROCESSING FLOW:
 *
 *   For each 3-byte entry:
 *     1. Read type (0=land, 1=loc)
 *     2. Read x coordinate
 *     3. Read z coordinate
 *     4. Route to appropriate handler:
 *        - type 0: map_send_land_data()
 *        - type 1: map_send_loc_data()
 *
 * PARAMETERS:
 *   player        - Requesting player
 *   in            - Input buffer with request data
 *   packet_length - Total packet length in bytes (must be multiple of 3)
 */
void map_handle_request(Player* player, StreamBuffer* in, i32 packet_length) {
    if (!player || !in) return;
    
    i32 entries = packet_length / 3;
    for (i32 i = 0; i < entries; i++) {
        u8 type = buffer_read_byte(in, false);
        u8 x = buffer_read_byte(in, false);
        u8 z = buffer_read_byte(in, false);
        
        if (type == 0) {
            map_send_land_data(player, x, z);
        } else {
            map_send_loc_data(player, x, z);
        }
    }
}

/*
 * FUNCTION: map_send_land_data
 *
 * Transmits land (terrain) file to client in chunks.
 *
 * CHUNKING ALGORITHM:
 *
 *   Why chunk files?
 *     - Socket buffers have limited size (typically 64KB)
 *     - Large files could overflow buffer
 *     - Chunking allows progress tracking
 *     - Prevents blocking on large transfers
 *
 *   Chunk size selection (1000 bytes):
 *     - Small enough: fits comfortably in any socket buffer
 *     - Large enough: minimizes packet overhead
 *     - Compatible: matches original client implementation
 *
 *   Algorithm steps:
 *     1. Read entire file into memory
 *     2. offset = 0
 *     3. While offset < total_size:
 *        a. remaining = min(1000, total_size - offset)
 *        b. Send DATA_LAND packet with:
 *           - File coordinates (x, z)
 *           - Current offset
 *           - Total file size
 *           - Chunk data (remaining bytes)
 *        c. offset += remaining
 *     4. Send DATA_LAND_DONE packet
 *
 * PACKET SEQUENCE EXAMPLE (5000-byte file):
 *
 *   Packet 1: DATA_LAND [x=50, z=50, offset=0, total=5000, data[0..999]]
 *   Packet 2: DATA_LAND [x=50, z=50, offset=1000, total=5000, data[1000..1999]]
 *   Packet 3: DATA_LAND [x=50, z=50, offset=2000, total=5000, data[2000..2999]]
 *   Packet 4: DATA_LAND [x=50, z=50, offset=3000, total=5000, data[3000..3999]]
 *   Packet 5: DATA_LAND [x=50, z=50, offset=4000, total=5000, data[4000..4999]]
 *   Packet 6: DATA_LAND_DONE [x=50, z=50]
 *
 *   Total packets: 6
 *   Total bytes sent: approximately 5000 + (6 * header_size)
 *
 * CLIENT REASSEMBLY:
 *
 *   The client uses offset and total_size to reconstruct the file:
 *     1. Allocate buffer of total_size
 *     2. For each DATA_LAND packet:
 *        - Copy chunk to buffer[offset]
 *     3. On DATA_LAND_DONE:
 *        - Verify CRC32
 *        - Save file to cache
 *
 * PARAMETERS:
 *   player - Player receiving the data
 *   file_x - File X coordinate
 *   file_z - File Z coordinate
 *
 * FILE NAMING:
 *   Format: m_X_Z
 *   Example: m_50_50 (land file for region 50,50)
 */
void map_send_land_data(Player* player, i32 file_x, i32 file_z) {
    if (!player || player->socket_fd < 0) return;
    
    char path[256];
    snprintf(path, sizeof(path), "data%smaps%sm%d_%d", 
             PATH_SEPARATOR, PATH_SEPARATOR, file_x, file_z);
    
    u8* data = NULL;
    u32 total_size = 0;
    read_file_to_buffer(path, &data, &total_size);
    
    const i32 CHUNK_SIZE = 1000;
    i32 offset = 0;
    
    while (offset < total_size && data != NULL) {
        i32 remaining = (total_size - offset) < CHUNK_SIZE ? (total_size - offset) : CHUNK_SIZE;
        
        StreamBuffer* out = buffer_create(remaining + 8);
        buffer_write_header_var(out, SERVER_DATA_LAND, player->out_cipher.initialized ? &player->out_cipher : NULL, VAR_SHORT);
        buffer_write_byte(out, file_x);
        buffer_write_byte(out, file_z);
        buffer_write_short(out, offset, BYTE_ORDER_BIG);
        buffer_write_short(out, total_size, BYTE_ORDER_BIG);
        
        for (i32 i = 0; i < remaining; i++) {
            buffer_write_byte(out, data[offset + i]);
        }
        
        buffer_finish_var_header(out, VAR_SHORT);
        network_send(player->socket_fd, out->data, out->position);
        buffer_destroy(out);
        
        offset += remaining;
    }
    
    if (data) free(data);
    
    /* Send completion packet */
    StreamBuffer* done = buffer_create(3);
    buffer_write_header(done, SERVER_DATA_LAND_DONE, player->out_cipher.initialized ? &player->out_cipher : NULL);
    buffer_write_byte(done, file_x);
    buffer_write_byte(done, file_z);
    network_send(player->socket_fd, done->data, done->position);
    buffer_destroy(done);
}

/*
 * FUNCTION: map_send_loc_data
 *
 * Transmits loc (location/object) file to client in chunks.
 *
 * IDENTICAL TO map_send_land_data except:
 *   - File prefix: l_X_Z instead of m_X_Z
 *   - Packet type: SERVER_DATA_LOC instead of SERVER_DATA_LAND
 *   - Done packet: SERVER_DATA_LOC_DONE instead of SERVER_DATA_LAND_DONE
 *
 * LOC FILE CONTENTS:
 *   Location files contain game object data:
 *     - Trees, rocks, ore deposits
 *     - Doors, gates, ladders
 *     - Furniture, decorations
 *     - Interactive objects
 *
 *   Each object has:
 *     - Object ID (what type of object)
 *     - Local position within region (0-63, 0-63)
 *     - Orientation (0-3, representing 0/90/180/270 degrees)
 *     - Type (wall, ground decoration, etc.)
 *
 * PARAMETERS:
 *   player - Player receiving the data
 *   file_x - File X coordinate
 *   file_z - File Z coordinate
 *
 * FILE NAMING:
 *   Format: l_X_Z
 *   Example: l_50_50 (loc file for region 50,50)
 */
void map_send_loc_data(Player* player, i32 file_x, i32 file_z) {
    if (!player || player->socket_fd < 0) return;
    
    char path[256];
    snprintf(path, sizeof(path), "data%smaps%sl%d_%d", 
             PATH_SEPARATOR, PATH_SEPARATOR, file_x, file_z);
    
    u8* data = NULL;
    u32 total_size = 0;
    read_file_to_buffer(path, &data, &total_size);
    
    const i32 CHUNK_SIZE = 1000;
    i32 offset = 0;
    
    while (offset < total_size && data != NULL) {
        i32 remaining = (total_size - offset) < CHUNK_SIZE ? (total_size - offset) : CHUNK_SIZE;
        
        StreamBuffer* out = buffer_create(remaining + 8);
        buffer_write_header_var(out, SERVER_DATA_LOC, player->out_cipher.initialized ? &player->out_cipher : NULL, VAR_SHORT);
        buffer_write_byte(out, file_x);
        buffer_write_byte(out, file_z);
        buffer_write_short(out, offset, BYTE_ORDER_BIG);
        buffer_write_short(out, total_size, BYTE_ORDER_BIG);
        
        for (i32 i = 0; i < remaining; i++) {
            buffer_write_byte(out, data[offset + i]);
        }
        
        buffer_finish_var_header(out, VAR_SHORT);
        network_send(player->socket_fd, out->data, out->position);
        buffer_destroy(out);
        
        offset += remaining;
    }
    
    if (data) free(data);
    
    /* Send completion packet */
    StreamBuffer* done = buffer_create(3);
    buffer_write_header(done, SERVER_DATA_LOC_DONE, player->out_cipher.initialized ? &player->out_cipher : NULL);
    buffer_write_byte(done, file_x);
    buffer_write_byte(done, file_z);
    network_send(player->socket_fd, done->data, done->position);
    buffer_destroy(done);
}
