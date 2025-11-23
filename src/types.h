/*******************************************************************************
 * TYPES.H - Fixed-Width Integer Types and Core Constants
 *******************************************************************************
 * 
 * EDUCATIONAL PURPOSE:
 * This file demonstrates fundamental concepts in:
 *   - Fixed-width integer types (guaranteed size across platforms)
 *   - Type portability (32-bit vs 64-bit systems)
 *   - Network protocol type safety
 *   - Memory alignment and size guarantees
 *   - Enum-based type systems vs preprocessor macros
 * 
 * CORE CONCEPT - WHY FIXED-WIDTH TYPES?:
 * 
 * In standard C, primitive type sizes are PLATFORM-DEPENDENT:
 *   
 *   TYPE      │ 32-bit System │ 64-bit System │ PROBLEM
 *   ──────────┼───────────────┼───────────────┼─────────────────────────
 *   char      │  1 byte       │  1 byte       │ Always 8 bits (OK)
 *   short     │  2 bytes      │  2 bytes      │ Usually OK, not guaranteed
 *   int       │  4 bytes      │  4 bytes      │ May be 2 bytes on old systems!
 *   long      │  4 bytes      │  8 bytes      │ DIFFERENT SIZE! (DANGER)
 *   long long │  8 bytes      │  8 bytes      │ Not in C89 standard
 *   size_t    │  4 bytes      │  8 bytes      │ Pointer-sized (varies)
 * 
 * NETWORK PROTOCOL NIGHTMARE:
 * Imagine sending a player's position from server to client:
 * 
 *   Server (64-bit Linux):    long x = 1234;  // 8 bytes
 *   Client (32-bit Windows):  long x;         // Reads 4 bytes, gets garbage!
 * 
 * SOLUTION - C99 stdint.h Fixed-Width Types:
 * 
 *   TYPE     │ SIZE    │ RANGE (UNSIGNED)      │ RANGE (SIGNED)
 *   ─────────┼─────────┼───────────────────────┼─────────────────────────
 *   uint8_t  │ 1 byte  │ 0 to 255              │ -128 to 127 (int8_t)
 *   uint16_t │ 2 bytes │ 0 to 65,535           │ -32,768 to 32,767
 *   uint32_t │ 4 bytes │ 0 to 4,294,967,295    │ -2,147,483,648 to ...
 *   uint64_t │ 8 bytes │ 0 to 18,446,744,073...│ -9,223,372,036,854...
 * 
 * GUARANTEE: These types are ALWAYS the specified width on ANY platform
 *            that supports them (all modern systems do).
 * 
 * TYPE ALIASES - WHY "u8" INSTEAD OF "uint8_t"?:
 * 
 * Readability and consistency with RuneScape protocol documentation:
 *   - RuneScape packets use "byte" (u8), "short" (u16), "int" (u32)
 *   - Shorter names reduce visual clutter
 *   - Industry standard in game development (Unity, Unreal use similar)
 * 
 * COMPARISON:
 *   ✗ verbose:  uint8_t buffer[MAX_PACKET_SIZE];
 *   ✓ concise:  u8 buffer[MAX_PACKET_SIZE];
 * 
 * MEMORY LAYOUT EXAMPLE - Packet Structure:
 * 
 * Consider a RuneScape packet with opcode, player ID, and position:
 * 
 *   struct Packet {
 *       u8  opcode;      // 1 byte   [offset 0]
 *       u16 player_id;   // 2 bytes  [offset 1-2]  (or offset 2-3 with padding!)
 *       u32 x_coord;     // 4 bytes  [offset 4-7]
 *       u32 y_coord;     // 4 bytes  [offset 8-11]
 *   };
 * 
 * ON DISK / NETWORK (no padding, total 13 bytes):
 *   ┌────┬─────────┬─────────────┬─────────────┐
 *   │ OP │ PLYR_ID │   X_COORD   │   Y_COORD   │
 *   └────┴─────────┴─────────────┴─────────────┘
 *    1B     2B         4B            4B
 * 
 * IN MEMORY (with alignment padding, may be 16 bytes):
 *   ┌────┬───┬─────────┬─────────────┬─────────────┐
 *   │ OP │PAD│ PLYR_ID │   X_COORD   │   Y_COORD   │
 *   └────┴───┴─────────┴─────────────┴─────────────┘
 *    1B   1B     2B         4B            4B
 *         ↑ Compiler inserts padding for alignment!
 * 
 * BYTE ORDER (ENDIANNESS):
 * 
 * Multi-byte integers can be stored two ways:
 * 
 *   Value: 0x12345678 (305,419,896 in decimal)
 * 
 *   BIG-ENDIAN (network byte order, most significant byte first):
 *     Address: [0x1000] [0x1001] [0x1002] [0x1003]
 *     Value:      0x12     0x34     0x56     0x78
 *     ────────────────────────────────────────────
 *     Reading left-to-right gives 12345678 (natural!)
 * 
 *   LITTLE-ENDIAN (x86/x64 native, least significant byte first):
 *     Address: [0x1000] [0x1001] [0x1002] [0x1003]
 *     Value:      0x78     0x56     0x34     0x12
 *     ────────────────────────────────────────────
 *     Reading left-to-right gives 78563412 (backwards!)
 * 
 * WHY IT MATTERS:
 *   - Network protocols use BIG-ENDIAN (RuneScape/network byte order convention)
 *   - x86/x64 CPUs use LITTLE-ENDIAN natively
 *   - Must convert when sending/receiving over network!
 * 
 * ENUM VS #DEFINE:
 * 
 * Two ways to define constants in C:
 * 
 *   1. PREPROCESSOR MACRO (#define):
 *      #define MAX_PLAYERS 2048
 *      - Text replacement before compilation
 *      - No type checking
 *      - No debugger support (replaced before compilation)
 *      - Can be redefined (dangerous!)
 * 
 *   2. ENUMERATION (enum):
 *      typedef enum { BYTE_ORDER_BIG = 0 } ByteOrder;
 *      - Real C type with type checking
 *      - Appears in debugger
 *      - Cannot be redefined
 *      - Compiler can optimize (same as #define in release builds)
 * 
 * WHEN TO USE EACH:
 *   - #define: Buffer sizes, limits (MAX_PLAYERS, MAX_PACKET_SIZE)
 *   - enum:    Related constants, states (ByteOrder, VarHeaderType)
 * 
 * HISTORICAL CONTEXT - RUNESCAPE PROTOCOL:
 * 
 * RuneScape (2004-2007 era) protocol specification:
 * This C server (rs225) connects to C client (Client3-main).
 * Implementation matches TypeScript server (Server-main) logic.
 * 
 * Original RuneScape (Java-based) used:
 *   - byte    = 8-bit signed   (-128 to 127)
 *   - short   = 16-bit signed  (-32768 to 32767)
 *   - int     = 32-bit signed
 *   - long    = 64-bit signed
 *   - BIG-ENDIAN byte order (network byte order)
 * 
 * Our C implementation must match the protocol specification exactly:
 *   - u8  ↔ unsigned byte  (0-255 range)
 *   - u16 ↔ unsigned short (0-65535 range)
 *   - u32 ↔ int (unsigned in protocol)
 *   - u64 ↔ long
 * 
 ******************************************************************************/

#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>   /* Fixed-width integer types (uint8_t, etc.) */
#include <stdbool.h>  /* Boolean type (bool, true, false) - C99 */
#include <stddef.h>   /* Standard definitions (size_t, NULL, offsetof) */

/*******************************************************************************
 * FIXED-WIDTH INTEGER TYPE ALIASES
 *******************************************************************************
 * 
 * UNSIGNED TYPES (always positive, 0 to MAX):
 *   u8:  1 byte  (0 to 255)                    - packet opcodes, bytes
 *   u16: 2 bytes (0 to 65,535)                 - player IDs, item IDs
 *   u32: 4 bytes (0 to 4,294,967,295)          - coordinates, ticks
 *   u64: 8 bytes (0 to 18,446,744,073,709...)  - timestamps, hashes
 * 
 * SIGNED TYPES (can be negative, MIN to MAX):
 *   i8:  1 byte  (-128 to 127)                 - relative offsets
 *   i16: 2 bytes (-32,768 to 32,767)           - coordinate deltas
 *   i32: 4 bytes (-2,147,483,648 to ...)       - game values
 *   i64: 8 bytes (-9,223,372,036,854... to ...) - large calculations
 * 
 * MEMORY SIZE:
 *   sizeof(u8)  = 1
 *   sizeof(u16) = 2
 *   sizeof(u32) = 4
 *   sizeof(u64) = 8
 *   (GUARANTEED on all platforms)
 * 
 * PORTABILITY:
 *   These types work identically on:
 *     - 32-bit ARM (Raspberry Pi)
 *     - 64-bit x86 (Intel/AMD)
 *     - 64-bit ARM (Apple M1/M2)
 *     - Embedded systems (microcontrollers)
 * 
 * USAGE GUIDELINES:
 *   - Use UNSIGNED (u8, u16, u32, u64) when:
 *       * Storing IDs, counts, sizes
 *       * Bit manipulation (flags, masks)
 *       * Network protocol fields (unsigned values)
 *   - Use SIGNED (i8, i16, i32, i64) when:
 *       * Values can be negative (deltas, offsets)
 *       * Mathematical operations (avoid underflow bugs)
 ******************************************************************************/

/* Unsigned fixed-width integers */
typedef uint8_t  u8;   /* 1 byte:  0..255 */
typedef uint16_t u16;  /* 2 bytes: 0..65535 */
typedef uint32_t u32;  /* 4 bytes: 0..4294967295 */
typedef uint64_t u64;  /* 8 bytes: 0..18446744073709551615 */

/* Signed fixed-width integers */
typedef int8_t   i8;   /* 1 byte:  -128..127 */
typedef int16_t  i16;  /* 2 bytes: -32768..32767 */
typedef int32_t  i32;  /* 4 bytes: -2147483648..2147483647 */
typedef int64_t  i64;  /* 8 bytes: -9223372036854775808..9223372036854775807 */

/*******************************************************************************
 * SERVER CAPACITY LIMITS
 *******************************************************************************
 * 
 * These constants define the maximum supported entities in the game world.
 * Chosen to balance memory usage vs gameplay capacity.
 ******************************************************************************/

/*
 * MAX_PLAYERS - Maximum concurrent players
 * 
 * VALUE: 2048 players
 * 
 * MEMORY IMPACT:
 *   Each player requires ~1KB of state (position, inventory, skills, etc.)
 *   Total player memory: 2048 * 1KB = 2MB (reasonable for modern servers)
 * 
 * NETWORK PROTOCOL:
 *   RuneScape uses 11-bit player indices in update packets
 *   11 bits = 2^11 = 2048 maximum (perfect match!)
 * 
 *   Example packet bit layout:
 *     ┌──────────────┬─────────────────┬───────────┐
 *     │ Player count │ Player 1 index  │ Update... │
 *     │   (8 bits)   │   (11 bits)     │           │
 *     └──────────────┴─────────────────┴───────────┘
 * 
 * HISTORICAL CONTEXT:
 *   - 2004 RuneScape: ~200 players per world (limited by server hardware)
 *   - 2007 RuneScape: ~2000 players per world (peak capacity)
 *   - Modern servers: 2048 chosen to align with protocol limits
 * 
 * WHY NOT MORE?:
 *   - Protocol constraint: 11-bit index cannot represent > 2047
 *   - Memory: Higher limits require more RAM
 *   - Performance: Update packets scale O(n^2) with visible players
 */
#define MAX_PLAYERS 2048

/*
 * MAX_PACKET_SIZE - Maximum network packet size in bytes
 * 
 * VALUE: 5000 bytes
 * 
 * WHY THIS SIZE?:
 *   - Largest RuneScape packet: Region update (map chunks)
 *   - Region = 104x104 tiles, each tile has 4 layers (ground, objects, etc.)
 *   - Worst case: ~4KB for full region + overhead
 *   - 5000 bytes provides safety margin
 * 
 * NETWORK IMPLICATIONS:
 *   - Fits in single TCP segment (most MTUs are 1500 bytes, but TCP handles
 *     fragmentation/reassembly automatically)
 *   - Larger than typical Ethernet MTU (1500), but that's OK - TCP handles it
 *   - Much smaller than TCP receive buffer (typically 64KB+)
 * 
 * SECURITY:
 *   - Prevents denial-of-service via huge packets
 *   - Server rejects any packet claiming length > MAX_PACKET_SIZE
 * 
 * MEMORY:
 *   - Each player has send/receive buffers
 *   - Total: 2048 players * 2 buffers * 5000 bytes = 20MB (acceptable)
 */
#define MAX_PACKET_SIZE 5000

/*
 * MAX_WAYPOINTS - Maximum queued movement destinations
 * 
 * VALUE: 100 waypoints
 * 
 * CONCEPT - WAYPOINT QUEUE:
 * When a player clicks multiple tiles rapidly (running), the client sends
 * each click as a waypoint. The server queues them and processes one per tick.
 * 
 *   Tick 0: Player at (10,10), queue=[{x:15,y:10}, {x:20,y:15}]
 *   Tick 1: Move to (11,10), queue=[{x:15,y:10}, {x:20,y:15}]
 *   ...
 *   Tick 5: Reach (15,10), queue=[{x:20,y:15}]
 *   Tick 10: Reach (20,15), queue=[]
 * 
 * MEMORY PER PLAYER:
 *   Each waypoint = 2 coordinates * 4 bytes = 8 bytes
 *   100 waypoints = 800 bytes per player
 * 
 * WHY 100?:
 *   - Normal clicking: ~5-10 waypoints queued
 *   - Rapid clicking: ~50 waypoints (spam protection)
 *   - 100 provides safety margin without wasting memory
 * 
 * OVERFLOW BEHAVIOR:
 *   - If queue full, drop oldest waypoint (FIFO)
 *   - Prevents memory exhaustion from malicious clients
 */
#define MAX_WAYPOINTS 100

/*
 * MAX_USERNAME_LENGTH - Maximum characters in a username
 * 
 * VALUE: 12 characters
 * 
 * HISTORICAL CONTEXT:
 *   - RuneScape Classic (2001): 12 character limit
 *   - Kept for consistency across all versions
 *   - Examples: "Zezima", "TheOldNite", "Bluerose13x"
 * 
 * CHARACTER SET:
 *   - Allowed: a-z, A-Z, 0-9, underscore, space
 *   - Not allowed: Special symbols (@, #, etc.)
 *   - Case-insensitive for lookups (stored as lowercase hash)
 * 
 * MEMORY:
 *   - Stored as null-terminated C string: 13 bytes (12 + '\0')
 *   - Also stored as 64-bit hash for fast lookup (8 bytes)
 * 
 * NETWORK ENCODING:
 *   - Sent as newline-terminated string (RuneScape protocol)
 *   - Example: "Zezima" → [0x5A 0x65 0x7A 0x69 0x6D 0x61 0x0A]
 *                           Z    e    z    i    m    a    \n
 */
#define MAX_USERNAME_LENGTH 12

/*******************************************************************************
 * NETWORK AND TIMING CONSTANTS
 ******************************************************************************/

/*
 * SERVER_PORT - TCP port for client connections
 * 
 * VALUE: 43594
 * 
 * HISTORICAL CONTEXT:
 *   - RuneScape 2004 era used port 43594
 *   - Chosen arbitrarily by Jagex (no special meaning)
 *   - Must be same on client and server to connect
 * 
 * PORT RANGES:
 *   - 0-1023:     Reserved (HTTP=80, HTTPS=443, SSH=22)
 *   - 1024-49151: Registered (MySQL=3306, MongoDB=27017)
 *   - 49152-65535: Dynamic/private (ephemeral ports)
 * 
 * 43594 is in the registered range - safe for private servers.
 * 
 * NETWORK BINDING:
 *   - Server calls bind(socket, port=43594)
 *   - Clients connect to server_ip:43594
 * 
 * FIREWALL:
 *   - Must allow TCP traffic on port 43594
 *   - Both inbound (server) and outbound (client)
 */
#define SERVER_PORT 43594

/*
 * TICK_RATE_MS - Game loop tick duration in milliseconds
 * 
 * VALUE: 600 milliseconds (0.6 seconds)
 * 
 * GAME TICK CONCEPT:
 *   The server runs a fixed-rate game loop:
 * 
 *   while (running) {
 *       process_packets();        // Handle player input
 *       update_movement();        // Move players/NPCs
 *       update_combat();          // Process damage
 *       send_updates();           // Sync state to clients
 *       sleep(TICK_RATE_MS);      // Wait 600ms
 *   }
 * 
 * IMPLICATIONS:
 *   - Players move 1 tile per tick (walking)
 *   - Players move 2 tiles per tick (running)
 *   - Combat hits occur on tick boundaries
 *   - Minimum latency for player action → server response: 600ms
 * 
 * WHY 600ms?:
 *   - Original RuneScape design (2004)
 *   - Slower than modern games (60 FPS = 16.7ms per frame)
 *   - Chosen for:
 *       * Reduced server CPU (fewer updates per second)
 *       * Turn-based feel (not twitch-based like FPS)
 *       * Works on slow connections (dial-up era)
 * 
 * TICK RATE vs FRAME RATE:
 *   - Server: 600ms tick = 1.67 ticks/second
 *   - Client: 50 FPS = smooth animation between ticks
 *   - Client interpolates movement between server updates
 * 
 * TIMING ACCURACY:
 *   - sleep(600) is not perfectly accurate (OS scheduling)
 *   - Real servers use high-resolution timers and drift correction
 */
#define TICK_RATE_MS 600

/*******************************************************************************
 * MAP AND WORLD CONSTANTS
 ******************************************************************************/

/*
 * MAP_SIZE - Dimensions of the game map in tiles
 * 
 * VALUE: 104 tiles (width and height of a region chunk)
 * 
 * WORLD STRUCTURE:
 *   RuneScape's world is divided into "regions" (square chunks):
 * 
 *   ┌──────────┬──────────┬──────────┐
 *   │ Region   │ Region   │ Region   │
 *   │ (52,52)  │ (53,52)  │ (54,52)  │  Each region = 104x104 tiles
 *   ├──────────┼──────────┼──────────┤
 *   │ Region   │ Region   │ Region   │
 *   │ (52,51)  │ (53,51)  │ (54,51)  │
 *   └──────────┴──────────┴──────────┘
 * 
 * REGION COORDINATES:
 *   - Absolute position: (x=5500, y=3300)
 *   - Region: (x/104, y/104) = (52, 31)
 *   - Local tile within region: (x%104, y%104) = (92, 76)
 * 
 * MEMORY:
 *   - 104x104 = 10,816 tiles per region
 *   - Each tile: 1 byte for type + 2 bytes for object ID = 3 bytes
 *   - Total per region: 32KB (fits in CPU cache!)
 * 
 * NETWORK:
 *   - When player enters new region, server sends entire 104x104 chunk
 *   - Client caches region and only requests updates on re-entry
 * 
 * WHY 104?:
 *   - Divisible by 8 (important for REGION_SIZE below)
 *   - Large enough to cover visible area (client renders ~50x50)
 *   - Small enough to send quickly (<5KB compressed)
 */
#define MAP_SIZE 104

/*
 * REGION_SIZE - Size of a region "sector" for spatial partitioning
 * 
 * VALUE: 8 tiles
 * 
 * CONCEPT - SPATIAL PARTITIONING:
 *   Divide each 104x104 region into 8x8 sectors for efficient lookups:
 * 
 *   104 / 8 = 13 sectors per dimension
 *   13 * 13 = 169 sectors per region
 * 
 *   ┌───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┐
 *   │ 0 │ 1 │ 2 │ 3 │ 4 │ 5 │ 6 │ 7 │ 8 │ 9 │10 │11 │12 │  (each = 8x8 tiles)
 *   ├───┼───┼───┼───┼───┼───┼───┼───┼───┼───┼───┼───┼───┤
 *   │13 │14 │...│   │   │   │   │   │   │   │   │   │   │
 *   └───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┘
 * 
 * USE CASE - COLLISION DETECTION:
 *   To find objects near player at (x=42, y=37):
 *     1. Calculate sector: (42/8, 37/8) = (5, 4)
 *     2. Only check objects in sector (5,4) and neighbors
 *     3. Much faster than checking all 10,816 tiles!
 * 
 * PERFORMANCE:
 *   - Without partitioning: O(n) search (n = objects in region)
 *   - With partitioning: O(n/169) average (only check ~1% of objects)
 * 
 * MEMORY:
 *   - Each sector has a linked list of objects
 *   - Pointer overhead: 169 sectors * 8 bytes = ~1.3KB per region
 */
#define REGION_SIZE 8

/*******************************************************************************
 * BYTE ORDER ENUMERATION
 *******************************************************************************
 * 
 * Defines the order of bytes in multi-byte integers (endianness).
 ******************************************************************************/

/*
 * ByteOrder - Byte ordering for multi-byte integers
 * 
 * USAGE: Passed to buffer read/write functions
 * 
 *   buffer_write_short(buf, 0x1234, BYTE_ORDER_BIG);
 *   → Writes [0x12][0x34]
 * 
 *   buffer_write_short(buf, 0x1234, BYTE_ORDER_LITTLE);
 *   → Writes [0x34][0x12]
 * 
 * VALUES:
 *   BYTE_ORDER_BIG    = 0   (network byte order, MSB first)
 *   BYTE_ORDER_LITTLE = 1   (x86 native order, LSB first)
 * 
 * WHEN TO USE EACH:
 *   - BIG_ENDIAN:    Network packets (RuneScape protocol)
 *   - LITTLE_ENDIAN: Files, platform-specific data
 * 
 * DETECTION AT RUNTIME:
 *   union { u32 i; u8 c[4]; } test = { .i = 0x01020304 };
 *   if (test.c[0] == 1)  → BIG_ENDIAN
 *   if (test.c[0] == 4)  → LITTLE_ENDIAN
 * 
 * COMPILER MACROS (not used here, for reference):
 *   #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
 *   #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
 */
typedef enum {
    BYTE_ORDER_BIG    = 0,  /* MSB first: 0x1234 → [0x12][0x34] */
    BYTE_ORDER_LITTLE = 1   /* LSB first: 0x1234 → [0x34][0x12] */
} ByteOrder;

/*******************************************************************************
 * VARIABLE-LENGTH PACKET HEADER TYPES
 *******************************************************************************
 * 
 * RuneScape packets come in three formats (see buffer.h for full details):
 *   1. Fixed-length (no length field)
 *   2. Variable-byte (1-byte length, max 255 bytes payload)
 *   3. Variable-short (2-byte length, max 65535 bytes payload)
 ******************************************************************************/

/*
 * VarHeaderType - Type of variable-length packet header
 * 
 * USAGE: Passed to buffer_write_header_var()
 * 
 *   buffer_write_header_var(buf, opcode=184, cipher, VAR_SHORT);
 *   buffer_write_int(buf, some_data, BIG);
 *   buffer_finish_var_header(buf, VAR_SHORT);
 * 
 * VALUES:
 *   VAR_BYTE  = 1   (1-byte length field, max 255 bytes payload)
 *   VAR_SHORT = 2   (2-byte length field, max 65535 bytes payload)
 * 
 * PACKET LAYOUTS:
 * 
 *   VAR_BYTE (max 255 bytes payload):
 *     ┌────────┬────────┬─────────────────┐
 *     │ opcode │ length │    payload      │
 *     │ 1 byte │ 1 byte │ 0-255 bytes     │
 *     └────────┴────────┴─────────────────┘
 * 
 *   VAR_SHORT (max 65535 bytes payload):
 *     ┌────────┬──────────────┬─────────────────┐
 *     │ opcode │    length    │    payload      │
 *     │ 1 byte │   2 bytes    │ 0-65535 bytes   │
 *     └────────┴──────────────┴─────────────────┘
 * 
 * WHEN TO USE EACH:
 *   - VAR_BYTE:  Chat messages, small updates (usually < 100 bytes)
 *   - VAR_SHORT: Region updates, large inventories (can be 1000+ bytes)
 * 
 * EXPLICIT VALUES:
 *   Values 1 and 2 match existing RuneScape protocol documentation.
 *   Could have been 0 and 1, but 1/2 is more intuitive (matches byte count).
 * 
 * TYPE SAFETY:
 *   Using enum instead of #define provides:
 *     - Compiler warnings if wrong type passed
 *     - Debugger shows "VAR_BYTE" instead of "1"
 *     - Cannot accidentally use 0 or 3 (invalid values)
 */
typedef enum {
    VAR_BYTE  = 1,  /* 1-byte length header (payload up to 255 bytes) */
    VAR_SHORT = 2   /* 2-byte length header (payload up to 65535 bytes) */
} VarHeaderType;

#endif /* TYPES_H */
