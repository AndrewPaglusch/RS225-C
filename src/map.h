/*******************************************************************************
 * MAP.H - RuneScape Map Region Management System
 *******************************************************************************
 *
 * PURPOSE:
 *   Manages the loading and transmission of map data to clients in the 
 *   RuneScape server. The map system is based on a region-based architecture
 *   where the world is divided into 64x64 tile chunks called "regions" or 
 *   "map files". This allows efficient streaming of terrain and object data
 *   as players move through the game world.
 *
 * KEY CONCEPTS:
 *
 *   1. REGION SYSTEM:
 *      - The game world is divided into 64x64 tile regions
 *      - Each region has two files: land (terrain) and loc (objects)
 *      - Regions are identified by file coordinates (x, z)
 *      - File coordinate = absolute coordinate / 64
 *
 *   2. MAP FILE TYPES:
 *      - Land files (m_X_Z): Terrain height, texture, overlay data
 *      - Loc files (l_X_Z): Location objects (trees, walls, furniture)
 *      - Both use the same coordinate system
 *
 *   3. STREAMING PROTOCOL:
 *      - Server sends LOAD_AREA packet with CRC checksums
 *      - Client requests missing/outdated files
 *      - Server sends data in 1000-byte chunks
 *      - Client caches validated files locally
 *
 *   4. COORDINATE SYSTEMS:
 *      - Absolute coordinates: Global tile position (0-16383)
 *      - File coordinates: Region identifier (abs / 64)
 *      - Local coordinates: Position within region (abs % 64)
 *
 * REGION LAYOUT:
 *
 *   The server maintains a 3x3 grid of regions around the player:
 *
 *        +-------+-------+-------+
 *        | NW    | N     | NE    |  Each box = 64x64 tile region
 *        | (-52) | (+0)  | (+52) |  Numbers show offset from center
 *        +-------+-------+-------+
 *        | W     |CENTER | E     |
 *        | (-52) | (+0)  | (+52) |
 *        +-------+-------+-------+
 *        | SW    | S     | SE    |
 *        | (-52) | (+0)  | (+52) |
 *        +-------+-------+-------+
 *
 *   The 52-tile offset ensures full coverage even when player is at
 *   region boundaries (64/2 rounded down = 32, but we use 52 for safety).
 *
 * PACKET STRUCTURE:
 *
 *   LOAD_AREA Packet (Server -> Client):
 *   +--------+--------+------------------------+
 *   | Header | RegionX| RegionY| File Entries  |
 *   | (var)  | (2B)   | (2B)   | (10B each)    |
 *   +--------+--------+--------+---------------+
 *
 *   Each File Entry:
 *   +------+------+-----------+-----------+
 *   | FileX| FileZ| Land CRC32| Loc CRC32 |
 *   | (1B) | (1B) | (4B)      | (4B)      |
 *   +------+------+-----------+-----------+
 *
 *   MAP_REQUEST Packet (Client -> Server):
 *   +--------+------------------------+
 *   | Header | Request Entries        |
 *   | (1B)   | (3B each)              |
 *   +--------+------------------------+
 *
 *   Each Request Entry:
 *   +------+------+------+
 *   | Type | FileX| FileZ|
 *   | (1B) | (1B) | (1B) |
 *   +------+------+------+
 *   Type: 0 = Land, 1 = Loc
 *
 *   DATA_LAND/DATA_LOC Packet (Server -> Client):
 *   +--------+------+------+--------+----------+-------------+
 *   | Header | FileX| FileZ| Offset | TotalSz  | Chunk Data  |
 *   | (var)  | (1B) | (1B) | (2B)   | (2B)     | (<=1000B)   |
 *   +--------+------+------+--------+----------+-------------+
 *
 * CRC32 VALIDATION:
 *
 *   The system uses CRC32 checksums to ensure data integrity:
 *   - Polynomial: 0xEDB88320 (IEEE 802.3 standard)
 *   - Initial value: 0xFFFFFFFF
 *   - Final XOR: 0xFFFFFFFF
 *   - Table-driven implementation for performance
 *
 *   CRC Algorithm Steps:
 *   1. Initialize CRC to 0xFFFFFFFF
 *   2. For each byte:
 *      a. XOR byte with low byte of CRC
 *      b. Look up result in precomputed table
 *      c. XOR with CRC shifted right 8 bits
 *   3. Invert final CRC value
 *
 * COORDINATE CONVERSION EXAMPLES:
 *
 *   Absolute Position -> File Coordinate:
 *     Player at (3200, 3200):
 *       File X = 3200 >> 6 = 50
 *       File Z = 3200 >> 6 = 50
 *       File name: m_50_50 (land), l_50_50 (loc)
 *
 *   Region Boundary Example:
 *     Player at (3264, 3200):  (at boundary of two regions)
 *       File X = 3264 >> 6 = 51
 *       File Z = 3200 >> 6 = 50
 *       Player crosses from region 50,50 to 51,50
 *
 *   Multiple Region Coverage:
 *     Player at center of region needs 9 files:
 *       Center: (50, 50)
 *       North: (50, 51), South: (50, 49)
 *       East: (51, 50), West: (49, 50)
 *       NE: (51, 51), NW: (49, 51)
 *       SE: (51, 49), SW: (49, 49)
 *
 * PERFORMANCE CHARACTERISTICS:
 *
 *   CRC32 Calculation:
 *     Time: O(n) where n = file size in bytes
 *     Space: O(1) - uses 256-entry lookup table (1KB)
 *     Typical file size: 2-10 KB per region
 *
 *   Region Loading:
 *     Files per load: 9 regions = 18 files (9 land + 9 loc)
 *     CRC calculations: Up to 18 CRC32 operations
 *     Network packets: 1 LOAD_AREA + variable DATA packets
 *
 *   Chunk Transmission:
 *     Chunk size: 1000 bytes maximum
 *     Packets per file: ceil(file_size / 1000)
 *     Example: 5KB file = 5 DATA packets + 1 DONE packet
 *
 * USAGE EXAMPLE:
 *
 *   // Player enters world at position (3200, 3200)
 *   Player* player = create_player();
 *   player->position.x = 3200;
 *   player->position.z = 3200;
 *
 *   // Calculate region coordinates
 *   i32 region_x = (i32)player->position.x >> 6;  // 50
 *   i32 region_y = (i32)player->position.z >> 6;  // 50
 *
 *   // Send initial map load (9 regions with CRCs)
 *   map_send_load_area(player, region_x, region_y);
 *
 *   // Client receives LOAD_AREA, checks CRCs
 *   // If files are missing or outdated, client sends MAP_REQUEST
 *
 *   // Server receives request and handles it
 *   map_handle_request(player, request_buffer, packet_length);
 *
 *   // For each requested file, server sends in chunks:
 *   // - Multiple DATA_LAND/DATA_LOC packets (1000 byte chunks)
 *   // - Final DATA_LAND_DONE/DATA_LOC_DONE packet
 *
 * FILE ORGANIZATION:
 *
 *   Map files are stored in: data/maps/
 *   - m_X_Z: Land/terrain files
 *   - l_X_Z: Location/object files
 *
 *   Example structure:
 *     data/maps/m_50_50  <- Terrain for region (50, 50)
 *     data/maps/l_50_50  <- Objects for region (50, 50)
 *     data/maps/m_50_51  <- Terrain for region (50, 51)
 *     data/maps/l_50_51  <- Objects for region (50, 51)
 *
 * EDUCATIONAL NOTES:
 *
 *   Why 64x64 regions?
 *   - Power of 2 enables fast division using bit shifts (>> 6)
 *   - Balances memory usage vs. load frequency
 *   - Each region approximately 2-10 KB (manageable size)
 *
 *   Why CRC32 validation?
 *   - Detects file corruption during download/storage
 *   - Fast to calculate (table-driven)
 *   - Industry standard (ZIP, PNG, Ethernet use same)
 *
 *   Why 1000-byte chunks?
 *   - Small enough to avoid socket buffer issues
 *   - Large enough to be efficient (minimize packet overhead)
 *   - Allows progress tracking for large files
 *
 *   Why 9 regions (3x3 grid)?
 *   - Ensures smooth streaming as player moves
 *   - Player can see 13-15 tiles in each direction
 *   - 3 regions = 192 tiles coverage (sufficient)
 *
 *******************************************************************************
 */

#ifndef MAP_H
#define MAP_H

#include "types.h"
#include "player.h"
#include "buffer.h"

/*
 * FUNCTION: map_calculate_crc32
 *
 * Calculates the CRC32 checksum of a data buffer using the IEEE 802.3 
 * polynomial (0xEDB88320). This checksum is used to validate map file
 * integrity and detect corruption.
 *
 * ALGORITHM:
 *   1. Initialize lookup table (one-time, 256 entries)
 *   2. Start with CRC = 0xFFFFFFFF
 *   3. For each byte in data:
 *      a. XOR byte with low 8 bits of CRC
 *      b. Shift CRC right 8 bits
 *      c. XOR with table entry
 *   4. Return inverted CRC
 *
 * PARAMETERS:
 *   data   - Pointer to data buffer to checksum
 *   length - Number of bytes in buffer
 *
 * RETURNS:
 *   32-bit CRC32 checksum value
 *
 * COMPLEXITY:
 *   Time: O(n) where n = length
 *   Space: O(1) - uses static 256-entry table
 *
 * EXAMPLE:
 *   u8 file_data[5000];
 *   u32 checksum = map_calculate_crc32(file_data, 5000);
 *   // Send checksum to client for validation
 */
u32 map_calculate_crc32(const u8* data, u32 length);

/*
 * FUNCTION: map_send_load_area
 *
 * Sends the LOAD_AREA packet to the client, informing them which map regions
 * they need to load. This packet includes CRC32 checksums for each file so
 * the client can determine which files to request.
 *
 * PROCESS:
 *   1. Calculate file coordinates from player position
 *   2. Determine 3x3 grid of regions around player (9 regions)
 *   3. Calculate CRC32 for each land and loc file (18 CRCs)
 *   4. Build LOAD_AREA packet with region coordinates and CRCs
 *   5. Send packet to client
 *
 * REGION CALCULATION:
 *   Center region: (abs_x >> 6, abs_z >> 6)
 *   Surrounding regions: offset by +/- 52 tiles in each direction
 *
 *   Visual layout:
 *     (-52,-52)  (-52,+0)  (-52,+52)
 *     (+0,-52)   (CENTER)  (+0,+52)
 *     (+52,-52)  (+52,+0)  (+52,+52)
 *
 * PARAMETERS:
 *   player   - Pointer to player receiving the map data
 *   region_x - Central region X coordinate (for client's local coordinate system)
 *   region_y - Central region Y coordinate (for client's local coordinate system)
 *
 * PACKET FORMAT:
 *   Variable-length short header
 *   2 bytes: region_x (big-endian)
 *   2 bytes: region_y (big-endian)
 *   For each file (up to 9):
 *     1 byte: file_x
 *     1 byte: file_z
 *     4 bytes: land_crc32 (big-endian)
 *     4 bytes: loc_crc32 (big-endian)
 *
 * EXAMPLE:
 *   Player at position (3200, 3200):
 *   - Center file: (50, 50)
 *   - Sends 9 region entries with CRCs
 *   - Client compares CRCs with cached files
 *   - Client requests files with mismatched/missing CRCs
 */
void map_send_load_area(Player* player, i32 region_x, i32 region_y);

/*
 * FUNCTION: map_handle_request
 *
 * Processes a MAP_REQUEST packet from the client. The client sends this
 * packet when it needs map files (either missing or CRC mismatch).
 *
 * PACKET STRUCTURE:
 *   Each entry is 3 bytes:
 *     Byte 0: Type (0 = land, 1 = loc)
 *     Byte 1: File X coordinate
 *     Byte 2: File Z coordinate
 *
 * PROCESS:
 *   1. Calculate number of entries (packet_length / 3)
 *   2. For each entry:
 *      a. Read type, x, z
 *      b. Call appropriate send function
 *
 * PARAMETERS:
 *   player        - Pointer to player requesting map data
 *   in            - Input buffer containing request packet
 *   packet_length - Total length of packet in bytes
 *
 * EXAMPLE REQUEST:
 *   Packet length: 9 bytes (3 entries)
 *   Entry 1: [0, 50, 50] -> Request land file m_50_50
 *   Entry 2: [1, 50, 50] -> Request loc file l_50_50
 *   Entry 3: [0, 51, 50] -> Request land file m_51_50
 */
void map_handle_request(Player* player, StreamBuffer* in, i32 packet_length);

/*
 * FUNCTION: map_send_land_data
 *
 * Transmits a land (terrain) file to the client in chunks. Land files contain
 * terrain height data, texture information, and ground overlays.
 *
 * CHUNKING ALGORITHM:
 *   1. Read entire file into memory
 *   2. Split into 1000-byte chunks
 *   3. Send each chunk in DATA_LAND packet with:
 *      - File coordinates (x, z)
 *      - Current offset
 *      - Total file size
 *      - Chunk data
 *   4. Send DATA_LAND_DONE when complete
 *
 * PACKET SEQUENCE:
 *   DATA_LAND (offset=0, size=5000, data[0-999])
 *   DATA_LAND (offset=1000, size=5000, data[1000-1999])
 *   DATA_LAND (offset=2000, size=5000, data[2000-2999])
 *   DATA_LAND (offset=3000, size=5000, data[3000-3999])
 *   DATA_LAND (offset=4000, size=5000, data[4000-4999])
 *   DATA_LAND_DONE
 *
 * PARAMETERS:
 *   player - Pointer to player receiving the data
 *   file_x - File X coordinate
 *   file_z - File Z coordinate
 *
 * FILE PATH:
 *   data/maps/m_X_Z
 *   Example: data/maps/m_50_50
 *
 * ERROR HANDLING:
 *   If file doesn't exist or can't be read, sends DONE packet immediately
 */
void map_send_land_data(Player* player, i32 file_x, i32 file_z);

/*
 * FUNCTION: map_send_loc_data
 *
 * Transmits a loc (location/object) file to the client in chunks. Loc files
 * contain game object data such as trees, rocks, doors, furniture, etc.
 *
 * IDENTICAL TO map_send_land_data but for loc files:
 *   - Uses l_X_Z instead of m_X_Z
 *   - Sends DATA_LOC instead of DATA_LAND packets
 *   - Sends DATA_LOC_DONE completion packet
 *
 * PARAMETERS:
 *   player - Pointer to player receiving the data
 *   file_x - File X coordinate
 *   file_z - File Z coordinate
 *
 * FILE PATH:
 *   data/maps/l_X_Z
 *   Example: data/maps/l_50_50
 */
void map_send_loc_data(Player* player, i32 file_x, i32 file_z);

/*
 * FUNCTION: map_get_file_coord
 *
 * Converts an absolute world coordinate to a file (region) coordinate.
 * This is the fundamental conversion for the region-based map system.
 *
 * CONVERSION:
 *   file_coord = abs_coord >> 6
 *   (equivalent to abs_coord / 64)
 *
 * BIT SHIFT EXPLANATION:
 *   Shifting right by 6 bits divides by 64 because:
 *   - Each bit position represents a power of 2
 *   - 2^6 = 64
 *   - Right shift by N divides by 2^N
 *
 *   Example in binary:
 *     3200 in binary: 110010000000
 *     >> 6:           000000110010 = 50
 *
 * PARAMETERS:
 *   abs_coord - Absolute world coordinate (0-16383)
 *
 * RETURNS:
 *   File coordinate (region identifier)
 *
 * EXAMPLES:
 *   map_get_file_coord(0)    = 0
 *   map_get_file_coord(63)   = 0
 *   map_get_file_coord(64)   = 1
 *   map_get_file_coord(3200) = 50
 *   map_get_file_coord(3264) = 51
 *
 * INVERSE OPERATION:
 *   To get region start from file coord:
 *   abs_coord = file_coord << 6  (multiply by 64)
 */
i32 map_get_file_coord(i32 abs_coord);

#endif /* MAP_H */
