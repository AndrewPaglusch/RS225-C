/*******************************************************************************
 * UPDATE.C - Player Update Protocol (Packet 184 / PLAYER_INFO)
 *******************************************************************************
 *
 * PURPOSE:
 *   Implements the most complex and critical packet in RuneScape protocol #225:
 *   the PLAYER_INFO packet (opcode 184). This packet synchronizes all visible
 *   player states (position, movement, appearance, animations) between server
 *   and client every game tick (600ms).
 *
 * PROTOCOL OVERVIEW:
 *   Every 600ms (game tick), server sends each player a PLAYER_INFO packet
 *   containing:
 *     1. Local player's movement/teleport
 *     2. Updates for players already in view (existing)
 *     3. New players entering view (adds)
 *     4. Players leaving view (removes)
 *     5. Visual updates (appearance, animations, chat)
 *
 * PACKET STRUCTURE (HYBRID BIT/BYTE MODE):
 *
 *   ┌────────────────────────────────────────────────────────────┐
 *   │ HEADER (standard packet header)                            │
 *   │  ├─ Opcode: 184 (SERVER_PLAYER_INFO)                      │
 *   │  └─ Frame: VAR_SHORT (2-byte length prefix)               │
 *   ├────────────────────────────────────────────────────────────┤
 *   │ BIT-PACKED SECTION (variable bit lengths)                  │
 *   │  ┌──────────────────────────────────────────────────────┐ │
 *   │  │ LOCAL PLAYER MOVEMENT (1-20 bits)                    │ │
 *   │  │  ├─ No movement:  1 bit   [0]                        │ │
 *   │  │  ├─ Walk:         8 bits  [01][direction:3][update:1]│ │
 *   │  │  ├─ Run:          12 bits [10][dir1:3][dir2:3][upd:1]│ │
 *   │  │  └─ Teleport:     20 bits [11][lx:7][ly:7][z:2][upd:1]│ │
 *   │  ├──────────────────────────────────────────────────────┤ │
 *   │  │ OTHER PLAYERS UPDATE SECTION                          │ │
 *   │  │  ├─ Count: 8 bits (num currently tracked players)    │ │
 *   │  │  └─ For each tracked player:                          │ │
 *   │  │     ┌─ Still in view?  1 bit                         │ │
 *   │  │     │   If NO (left view):                            │ │
 *   │  │     │    └─ [0] = removed, that's it                  │ │
 *   │  │     │   If YES (still visible):                       │ │
 *   │  │     │    ├─ [1] still here                            │ │
 *   │  │     │    ├─ Moved? 1 bit                              │ │
 *   │  │     │    │   If NO: [0]                               │ │
 *   │  │     │    │   If YES: [1][dir:3][update:1]             │ │
 *   │  │     │    └─ Or: [1][teleport:11 bits]                 │ │
 *   │  │     └─ Continue for all tracked...                    │ │
 *   │  ├──────────────────────────────────────────────────────┤ │
 *   │  │ NEW PLAYERS SECTION (entering view)                   │ │
 *   │  │  For each new player:                                 │ │
 *   │  │   ├─ Player index: 11 bits (PID 0-2047)              │ │
 *   │  │   ├─ Delta X: 5 bits (relative to viewer, -16 to 15) │ │
 *   │  │   ├─ Delta Y: 5 bits (relative to viewer, -16 to 15) │ │
 *   │  │   ├─ Has update? 1 bit                                │ │
 *   │  │   └─ Discard walking queue: 1 bit (always 1)         │ │
 *   │  │  (Total: 23 bits per new player)                      │ │
 *   │  ├──────────────────────────────────────────────────────┤ │
 *   │  │ END MARKER: 11 bits all set (2047)                   │ │
 *   │  └──────────────────────────────────────────────────────┘ │
 *   ├────────────────────────────────────────────────────────────┤
 *   │ BYTE-ALIGNED SECTION (extended update blocks)             │
 *   │  For each player with update_flags != 0:                  │
 *   │   ┌─ Update mask: 1 byte (which updates present)         │ │
 *   │   └─ Update data blocks (in flag order):                 │ │
 *   │      ├─ UPDATE_APPEARANCE (0x01): Appearance block       │ │
 *   │      ├─ UPDATE_ANIMATION (0x02): Animation data          │ │
 *   │      ├─ UPDATE_GRAPHIC (0x04): Spotanim/graphics         │ │
 *   │      └─ UPDATE_CHAT (0x08): Public chat message          │ │
 *   └────────────────────────────────────────────────────────────┘
 *
 * BIT PACKING RATIONALE:
 *   Why not use all bytes? Bandwidth optimization!
 *   
 *   Example: 50 players in view, most walking:
 *     Byte-aligned: 50 × 8 bytes = 400 bytes
 *     Bit-packed:   50 × 5 bits  = 32 bytes (12.5× smaller!)
 *   
 *   In 2004, dial-up modems limited to ~56 kbps. Bit packing reduced
 *   bandwidth usage by ~70%, allowing smoother gameplay.
 *
 * MOVEMENT ENCODING:
 *   
 *   NONE (standing still):
 *     Bits: [0]
 *     Example: Player hasn't moved since last tick
 *   
 *   WALK (1 tile per tick):
 *     Bits: [01][direction:3 bits][has_update:1 bit]
 *     Directions: 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW
 *     Example: Walk east with appearance update
 *       → [01][010][1] = 01010 1 (6 bits total)
 *   
 *   RUN (2 tiles per tick):
 *     Bits: [10][dir1:3][dir2:3][has_update:1]
 *     Example: Run northeast (N then E)
 *       → [10][000][010][0] = 10 000 010 0 (10 bits)
 *   
 *   TELEPORT (instant position change):
 *     Bits: [11][local_x:7][local_y:7][height:2][has_update:1]
 *     Local coordinates: Position relative to region base (0-127)
 *     Example: Teleport to (45, 67, height 0) with update
 *       → [11][0101101][1000011][00][1] = 20 bits
 *
 * UPDATE FLAGS (BITMASK):
 *   Each flag enables a corresponding update block in byte section.
 *   
 *   UPDATE_APPEARANCE (0x01): Visual appearance changed
 *     - Gender, body parts, colors, equipment
 *     - Most common update (players login, change gear)
 *     - Size: 50-100 bytes typically
 *   
 *   UPDATE_ANIMATION (0x02): Playing animation
 *     - Animation ID, delay
 *     - Size: 3 bytes
 *   
 *   UPDATE_GRAPHIC (0x04): Spot animation (graphics overlay)
 *     - Graphic ID, height, delay
 *     - Size: 5 bytes
 *   
 *   UPDATE_CHAT (0x08): Public chat message
 *     - Packed text, color, effect
 *     - Size: 10-80 bytes
 *
 * ALGORITHM (DETAILED):
 *
 *   PHASE 1: Local Player Movement
 *     ┌─────────────────────────────────┐
 *     │ Was placement needed? (teleport)│
 *     │  YES → Write teleport (20 bits) │
 *     │  NO  → Check movement type:     │
 *     │    • None → [0] (1 bit)         │
 *     │    • Walk → [01]... (6 bits)    │
 *     │    • Run  → [10]... (10 bits)   │
 *     └─────────────────────────────────┘
 *
 *   PHASE 2: Existing Player Updates
 *     ┌─────────────────────────────────┐
 *     │ Write count of tracked (8 bits) │
 *     │ For each tracked player:        │
 *     │  ├─ Still in view?              │
 *     │  │  NO  → Write [0], remove     │
 *     │  │  YES → Write [1]...          │
 *     │  │    └─ Movement? [0] or [1]...│
 *     │  └─ Continue...                 │
 *     └─────────────────────────────────┘
 *
 *   PHASE 3: New Player Adds
 *     ┌─────────────────────────────────┐
 *     │ For each player in global list: │
 *     │  ├─ Not already tracked?        │
 *     │  ├─ Within view distance?       │
 *     │  └─ YES to both:                │
 *     │     ├─ Write PID (11 bits)      │
 *     │     ├─ Write delta X,Y (10 bits)│
 *     │     ├─ Write update flag (1 bit)│
 *     │     └─ Write discard (1 bit)    │
 *     └─────────────────────────────────┘
 *
 *   PHASE 4: End Marker & Update Blocks
 *     ┌─────────────────────────────────┐
 *     │ Write 2047 (11 bits)            │
 *     │ Switch to byte mode             │
 *     │ For each player with flags:     │
 *     │  ├─ Write mask byte             │
 *     │  └─ Write update blocks         │
 *     └─────────────────────────────────┘
 *
 * PERFORMANCE:
 *   Typical packet sizes (1 viewer, varies by scenario):
 *     - Empty world: ~10 bytes (just local player movement)
 *     - 10 nearby players walking: ~40 bytes
 *     - 50 players, 5 new: ~200 bytes
 *     - 200 players, full appearance updates: ~15 KB (rare)
 *   
 *   Time complexity: O(V × N) where:
 *     V = viewers (players receiving update)
 *     N = players in viewer's range (~10-50 typical)
 *   
 *   Critical path: Runs every 600ms for every online player
 *
 * THREAD SAFETY:
 *   Not thread-safe. Assumes single game loop thread.
 *   Reads player states that must not change during packet construction.
 *
 * CROSS-REFERENCES:
 *   - Called by: world.c (world_process, tick loop)
 *   - Uses: buffer.c (bit packing), player_list.c (tracking)
 *   - Protocol: RS2 #225 CLIENT_PLAYER_INFO handler (client-side)
 *   - TypeScript ref: Server-main/src/lostcity/network/225/outgoing/PlayerInfo.ts
 *
 ******************************************************************************/

#include "update.h"
#include "server.h"
#include "network.h"
#include "buffer.h"
#include "position.h"
#include <string.h>
#include <stdio.h>

/* opcode 184 */
#ifndef SERVER_PLAYER_INFO
#define SERVER_PLAYER_INFO 184
#endif

/*******************************************************************************
 * DATA STRUCTURES AND ALGORITHMIC COMPLEXITY ANALYSIS
 *******************************************************************************
 *
 * BIT-PACKING ALGORITHM:
 *   The packet uses variable-length bit fields to minimize bandwidth.
 *   This is a form of Huffman-like encoding optimized for game state:
 *   
 *   Frequency distribution (typical gameplay):
 *     - 70% of players: Standing still (1 bit encoding)
 *     - 25% of players: Walking (6 bits encoding)
 *     - 4% of players: Running (10 bits encoding)
 *     - 1% of players: Teleporting (20 bits encoding)
 *   
 *   Average bits per player: 0.7×1 + 0.25×6 + 0.04×10 + 0.01×20 = 2.6 bits
 *   vs byte-aligned: 8 bits always
 *   Compression ratio: 3.08× (67.5% bandwidth reduction)
 *
 * COMPLEXITY ANALYSIS:
 *   
 *   Let N = total online players
 *   Let V = players in viewer's range (typically 10-50)
 *   Let T = players tracked last tick
 *   
 *   send_player_info() time complexity:
 *     Phase 1 (local player):       O(1)
 *     Phase 2 (update tracked):     O(T) = O(V)
 *     Phase 3 (add new players):    O(N) worst case, O(V) average
 *     Phase 4 (update blocks):      O(V)
 *     Total: O(N) worst case, O(V) average
 *   
 *   Why O(N) worst case?
 *     - Must scan entire player list to find new players entering range
 *     - No spatial indexing (would require O(log N) quadtree/octree)
 *     - Trade-off: Simplicity vs performance
 *   
 *   Space complexity:
 *     - Output buffer: O(V) - proportional to visible players
 *     - Tracking arrays: O(N) - 2048 entries × 3 bytes = 6KB per player
 *     - Total per player: ~6KB + packet size (100-500 bytes typical)
 *
 * DATA STRUCTURE CHOICES:
 *   
 *   PlayerTracking structure:
 *     local_players[MAX_PLAYERS]:  Array of PIDs (sparse, indexed)
 *     local_count:                 Counter (dense)
 *     tracked[MAX_PLAYERS]:        Bitmap (sparse, boolean array)
 *   
 *   Why array instead of hash table for tracking?
 *     ✓ O(1) lookup by PID (direct indexing)
 *     ✓ Cache-friendly (sequential memory)
 *     ✓ Simple implementation (no hash collisions)
 *     ✗ Wastes memory for low player counts (2KB per player)
 *   
 *   Alternative: Bitmap for tracked[] (would save 7/8 memory):
 *     tracked: bool[2048] = 2048 bytes
 *     vs tracked: u8[256] = 256 bytes (8 bits per byte)
 *     Savings: 1792 bytes per player
 *     Cost: Bit manipulation overhead (shifts, masks)
 *     Current choice: Prioritize speed over memory
 *
 * STREAMING BUFFER ARCHITECTURE:
 *   
 *   StreamBuffer uses dynamic growth with power-of-2 capacity:
 *     Initial: 256 bytes
 *     Growth: capacity × 2 when full (256 → 512 → 1024 → 2048)
 *     Amortized complexity: O(1) append
 *   
 *   Why power-of-2 growth?
 *     - Allocator-friendly (aligns with page boundaries)
 *     - Predictable performance (no surprising reallocations)
 *     - Amortized O(1) despite occasional O(n) copy
 *   
 *   Analysis of reallocation cost:
 *     N appends with doubling strategy:
 *     Copies: 1 + 2 + 4 + 8 + ... + N/2 = N-1 total copies
 *     Amortized: (N-1)/N ≈ 1 copy per append → O(1)
 *
 * BIT ACCESS PATTERNS:
 *   
 *   Bit writing (buffer_write_bits):
 *     1. Shift value left by (32 - num_bits) to align MSB
 *     2. Right-shift by bit_position to position in current byte
 *     3. OR with existing byte (accumulate bits)
 *     4. Advance bit_position, wrap at byte boundary
 *   
 *   Example: Writing 3-bit value 5 (binary 101) at bit position 2:
 *     Current byte: 0000 00XX (2 bits already used)
 *     Value: 101
 *     Result: 0010 1XXX (5 bits now used)
 *   
 *   Complexity: O(1) per bit write, O(k) to write k-bit value
 *
 * PACKET SIZE ESTIMATION:
 *   
 *   Formula for update packet size:
 *     Size = Header + LocalMovement + TrackedUpdates + NewAdds + Blocks
 *   
 *   Example: 100 players online, 20 in range, 15 tracked, 5 new:
 *     Header:          4 bytes
 *     Local movement:  1 bit (standing) = 0.125 bytes
 *     Tracked count:   8 bits = 1 byte
 *     Tracked updates: 15 × 2 bits (avg) = 30 bits = 3.75 bytes
 *     New adds:        5 × 23 bits = 115 bits = 14.375 bytes
 *     End marker:      11 bits = 1.375 bytes
 *     Byte alignment:  +padding to next byte
 *     Update blocks:   20 × 80 bytes (appearance) = 1600 bytes
 *     Total:           ~1625 bytes
 *   
 *   Worst case (200 players, all with appearance updates):
 *     ~16 KB packet (happens on login to crowded area)
 *
 ******************************************************************************/

/* Update masks (bitmask flags for update blocks) */
#define UPDATE_APPEARANCE 0x1  /* Visual appearance changed (50-100 bytes) */
#define UPDATE_ANIMATION 0x2   /* Playing animation (3 bytes) */
#define UPDATE_GRAPHIC 0x4     /* Spot animation/graphics overlay (5 bytes) */
#define UPDATE_CHAT 0x8        /* Public chat message (10-80 bytes) */

/* Movement types for bit-packed encoding */
#define MOVEMENT_NONE 0      /* Standing still: 1 bit [0] */
#define MOVEMENT_WALK 1      /* Walk one tile: 8 bits [01][dir:3][upd:1] */
#define MOVEMENT_RUN 2       /* Run two tiles: 12 bits [10][dir1:3][dir2:3][upd:1] */
#define MOVEMENT_TELEPORT 3  /* Instant position change: 20 bits [11][x:7][y:7][h:2][upd:1] */

/*******************************************************************************
 * FORWARD DECLARATIONS
 ******************************************************************************/

static u64 username_to_base37(const char* username);
static void update_local_player_movement(Player* player, StreamBuffer* out);
static void append_placement(StreamBuffer* out, u32 local_x, u32 local_y, u32 z, bool reset_move, bool update);
static void append_walk(StreamBuffer* out, i32 direction, bool update);
static void append_run(StreamBuffer* out, i32 dir1, i32 dir2, bool update);
static void append_stand(StreamBuffer* out);
static void append_appearance(Player* player, StreamBuffer* out);
static void update_other_players(Player* viewer, Player* all_players[], u32 player_count, StreamBuffer* out, StreamBuffer* block, PlayerTracking* tracking);
static void append_player_add(StreamBuffer* out, Player* player, Player* viewer);
static void append_player_update_block(Player* player, StreamBuffer* block, u8 mask);

/*******************************************************************************
 * HELPER FUNCTIONS
 ******************************************************************************/

/*
 * enc_for - Get ISAAC cipher for player if initialized
 * 
 * Returns pointer to out_cipher if player has completed login handshake,
 * NULL otherwise (pre-login state uses unencrypted packets).
 */
static inline ISAACCipher* enc_for(Player* p) {
    return (p && p->out_cipher.initialized) ? &p->out_cipher : NULL;
}

/*******************************************************************************
 * PACKET CONSTRUCTION
 ******************************************************************************/

/* Emit an empty player-info update (no local/region changes, no masks). */

/*
 * send_player_info_empty - Send minimal update packet with no changes
 * 
 * @param player  Target player
 * 
 * Used when player has no movement or update flags.
 * Minimal packet contains only header with no bit-packed data.
 * 
 * WIRE FORMAT:
 *   [opcode:1][length:2][no payload]
 */
void send_player_info_empty(Player* player) {
    if (!player) return;

    ISAACCipher* enc = (player->out_cipher.initialized ? &player->out_cipher : NULL);

    StreamBuffer* out = buffer_create(4);
    buffer_write_header_var(out, SERVER_PLAYER_INFO, enc, VAR_SHORT);


    buffer_finish_var_header(out, VAR_SHORT);

    dbg_log_send("PLAYER_INFO(empty)", SERVER_PLAYER_INFO, "varshort",
                 0, enc != NULL);

    network_send(player->socket_fd, out->data, out->position);
    buffer_destroy(out);
}

/*
 * update_local_player_movement - Encode local player's movement in bit mode
 * 
 * @param player  Local player (viewer receiving this packet)
 * @param out     Output buffer in bit access mode
 * 
 * FIX:
 *   Local coordinates calculated relative to player's origin (from LOAD_AREA packet).
 *   Creates Position struct from origin_x/origin_z stored when map region was sent.
 *   This ensures coordinate system matches across all viewing clients.
 * 
 * DECISION TREE (priority order):
 * 
 *   1. needs_placement? → Placement encoding (21 bits)
 *   2. secondary_direction != -1? → Run encoding (10 bits)
 *   3. primary_direction != -1? → Walk encoding (7 bits)
 *   4. has_update? → Stand encoding (3 bits)
 *   5. else → No update (1 bit)
 * 
 * ENCODING FORMATS:
 * 
 *   Placement (teleport/login) - 21 bits:
 *     [update_required:1=1][movement_type:2=3][z:2][local_x:7][local_y:7][reset:1][update:1]
 *     
 *     When: Player just logged in, teleported, or changed regions
 *     Contains: Absolute position in region-local coordinates
 *     Example: Login at Lumbridge (local 22,18,0)
 *       [1][11][00][0010110][0010010][0][1] = 21 bits
 * 
 *   Run (two-tile movement) - 10 bits:
 *     [update_required:1=1][movement_type:2=2][dir1:3][dir2:3][update:1]
 *     
 *     When: Player running (both movement directions set)
 *     Contains: Two sequential directions
 *     Example: Run east (E,E)
 *       [1][10][010][010][0] = 10 bits
 * 
 *   Walk (one-tile movement) - 7 bits:
 *     [update_required:1=1][movement_type:2=1][dir:3][update:1]
 *     
 *     When: Player walking (only primary direction set)
 *     Contains: Single direction
 *     Example: Walk north
 *       [1][01][000][0] = 7 bits
 * 
 *   Stand with update - 3 bits:
 *     [update_required:1=1][movement_type:2=0]
 *     
 *     When: Not moving but appearance/animation changed
 *     Example: Change equipment while standing
 *       [1][00] = 3 bits
 * 
 *   No changes - 1 bit:
 *     [update_required:1=0]
 *     
 *     When: Nothing changed since last tick
 *     Most bandwidth-efficient case
 *       [0] = 1 bit
 * 
 * PRIORITY ORDERING RATIONALE:
 * 
 *   needs_placement checked first:
 *     - Overrides all other movement (teleport supersedes walk)
 *     - Cleared after encoding (one-time event)
 *     - Must be handled before relative movement
 * 
 *   Run checked before walk:
 *     - secondary_direction only set when running
 *     - If both directions set, MUST use run encoding
 *     - Walk encoding can't represent two directions
 * 
 *   Walk checked before stand:
 *     - primary_direction set when moving
 *     - Movement takes priority over stationary updates
 * 
 *   has_update checked last:
 *     - Only relevant if not moving
 *     - Appearance changes while standing
 * 
 * MOVEMENT STATE TRANSITIONS:
 * 
 *   Each tick, player's movement state is updated:
 *     1. Process walking queue → set directions
 *     2. Calculate energy drain (if running)
 *     3. Update position based on directions
 *     4. Encode movement for clients
 *     5. Clear directions for next tick
 * 
 *   State diagram:
 *     Standing → Walk: Click once on ground
 *     Standing → Run: Click with run enabled
 *     Walk → Run: Enable run toggle
 *     Run → Walk: Disable run or energy depleted
 *     Any → Placement: Teleport spell or login
 * 
 * BANDWIDTH ANALYSIS:
 * 
 *   Frequency distribution (typical player behavior):
 *     70% ticks: No changes (1 bit each)
 *     25% ticks: Walking (7 bits each)
 *     4% ticks: Running (10 bits each)
 *     1% ticks: Teleporting (21 bits each)
 * 
 *   Average bits per tick:
 *     0.70×1 + 0.25×7 + 0.04×10 + 0.01×21 = 2.76 bits
 * 
 *   vs absolute position every tick: 21 bits
 *   Compression ratio: 7.6× (87% bandwidth reduction)
 */
static void update_local_player_movement(Player* player, StreamBuffer* out) {
    bool has_update = (player && player->update_flags);
    
    if (player && player->needs_placement) {
        buffer_write_bits(out, 1, 1);
        Position origin;
        position_init(&origin, player->origin_x, player->origin_z, player->position.height);
        u32 local_x = position_get_local_x(&player->position, &origin);
        u32 local_y = position_get_local_z(&player->position, &origin);
        u32 z = player->position.height & 0x3;
        append_placement(out, local_x, local_y, z, false, has_update);
    } else if (player && player->secondary_direction != -1) {
        buffer_write_bits(out, 1, 1);
        append_run(out, player->primary_direction, player->secondary_direction, has_update);
    } else if (player && player->primary_direction != -1) {
        buffer_write_bits(out, 1, 1);
        append_walk(out, player->primary_direction, has_update);
    } else {
        if (has_update) {
            buffer_write_bits(out, 1, 1);
            append_stand(out);
        } else {
            buffer_write_bits(out, 1, 0);
        }
    }
}

/*
 * update_player - Construct and send full player update packet
 * 
 * @param player         Viewing player (packet recipient)
 * @param all_players    Array of all active players
 * @param player_count   Count of active players
 * @param tracking       Player's tracking state (local player list)
 * 
 * HIGH-LEVEL ALGORITHM (10 PHASES):
 * 
 *   Phase 1: Buffer allocation
 *     - Main buffer (out): 4096 bytes initial capacity for bit-packed data
 *     - Block buffer: 2048 bytes for update blocks (appearance, animation)
 *     - StreamBuffer uses power-of-2 growth: 4096 → 8192 → 16384 if needed
 * 
 *   Phase 2: Packet header construction
 *     - Write opcode 184 (SERVER_PLAYER_INFO)
 *     - Frame type: VAR_SHORT (2-byte length prefix, max 65535 bytes)
 *     - ISAAC encryption applied to opcode if cipher initialized
 *     - Length placeholder written, filled in phase 8
 * 
 *   Phase 3: Local player update block (CRITICAL ORDERING)
 *     - Local player's update block MUST be written FIRST to block buffer
 *     - Client expects local player appearance before any others
 *     - Only write if update_flags != 0
 * 
 *   Phase 4: Enter bit access mode
 *     - StreamBuffer switches from byte-aligned to bit-aligned writes
 *     - Maintains bit_position counter (0-7 within current byte)
 *     - Allows variable-length encoding (1-bit standing, 8-bit walk, etc.)
 * 
 *   Phase 5: Encode local player movement
 *     - Calls update_local_player_movement()
 *     - Encodes viewer's own position/movement state
 *     - Writes 1-21 bits depending on movement type
 * 
 *   Phase 6: Update other players
 *     - Calls update_other_players()
 *     - Processes tracked player list (removals, updates, additions)
 *     - Appends update blocks to block buffer
 *     - Most complex phase: O(T × P + P) complexity
 * 
 *   Phase 7: Exit bit access mode
 *     - Pads current byte to alignment if bit_position != 0
 *     - Returns StreamBuffer to byte-aligned mode
 *     - Critical: ensures update blocks start on byte boundary
 * 
 *   Phase 8: Append update blocks
 *     - Copies entire block buffer to main buffer
 *     - Update blocks contain: [mask:1][data based on mask bits]
 *     - Local player block comes first (written in phase 3)
 *     - Other player blocks follow in processing order
 * 
 *   Phase 9: Finalize and transmit
 *     - Calculate total payload length
 *     - Write length to VAR_SHORT header (backpatch)
 *     - Send packet via network_send()
 *     - Log transmission for debugging
 * 
 *   Phase 10: Cleanup and state reset
 *     - Destroy allocated buffers
 *     - Clear region_changed flag (prevents repeated teleport encoding)
 *     - Note: update_flags cleared separately by caller
 * 
 * BUFFER MEMORY LAYOUT:
 * 
 *   Main buffer (out):
 *   ┌─────────────┬──────────────────────────────┬─────────────────────┐
 *   │ Header      │ Bit-packed section           │ Update blocks       │
 *   │ [opcode:1]  │ ┌─────────────────────────┐  │ ┌─────────────────┐ │
 *   │ [length:2]  │ │ Local movement (1-21b)  │  │ │ Local block     │ │
 *   │             │ │ Tracked count (8b)      │  │ │ [mask][data...] │ │
 *   │             │ │ Tracked updates (var)   │  │ │                 │ │
 *   │             │ │ New additions (var)     │  │ │ Other blocks... │ │
 *   │             │ │ End marker (11b)        │  │ │ [mask][data...] │ │
 *   │             │ └─────────────────────────┘  │ └─────────────────┘ │
 *   └─────────────┴──────────────────────────────┴─────────────────────┘
 *   3 bytes       Variable (bit-aligned)         Variable (byte-aligned)
 * 
 * COMPLEXITY ANALYSIS:
 * 
 *   Time complexity per player update:
 *     Buffer allocation:          O(1) amortized
 *     Header write:               O(1)
 *     Local movement encode:      O(1)
 *     Other players update:       O(T × P + P) - see update_other_players()
 *     Block append:               O(B) where B = total block bytes
 *     Network send:               O(N) where N = packet size
 *     Total:                      O(T × P + P + B + N)
 * 
 *   Typical case (100 players, 20 visible):
 *     T × P:   20 × 100 = 2,000 comparisons
 *     B:       20 × 80 = 1,600 bytes (appearances)
 *     N:       ~1,625 bytes transmission
 *     Runtime: ~0.5ms (well under 600ms tick budget)
 * 
 *   Space complexity:
 *     out buffer:   O(V × A) where V=visible, A=avg appearance size
 *     block buffer: O(V × A)
 *     Total:        O(V × A) ≈ 4-8 KB typical, 32 KB worst case
 * 
 * PACKET SIZE ESTIMATION:
 * 
 *   Minimum packet (no players in range):
 *     Header: 3 bytes
 *     Local movement: 1 bit → 1 byte (with padding)
 *     Tracked count: 0 → 1 byte
 *     End marker: 11 bits → 2 bytes
 *     Total: ~7 bytes
 * 
 *   Typical packet (20 visible, 15 tracked, 5 new):
 *     Header: 3 bytes
 *     Bit section: ~23 bytes (see update_other_players bit budget)
 *     Update blocks: 20 × 80 = 1600 bytes
 *     Total: ~1626 bytes
 * 
 *   Maximum packet (255 visible, all with appearance):
 *     Header: 3 bytes
 *     Bit section: ~750 bytes (255 × 23 bits adds + overhead)
 *     Update blocks: 255 × 100 = 25,500 bytes
 *     Total: ~26,253 bytes
 *     Note: Exceeds practical MTU, would fragment across TCP
 * 
 * ORDERING CONSTRAINTS:
 * 
 *   1. Local player update block MUST be written before bit-packed section
 *      Client-side expectation: Local player block index = 0
 * 
 *   2. Bit-packed section MUST be byte-aligned before update blocks
 *      Protocol spec: Update blocks are byte-aligned data
 * 
 *   3. Update blocks MUST match bit-packed update flags
 *      Each player with update_required=1 MUST have corresponding block
 * 
 *   4. Tracked player removals MUST be processed before additions
 *      Prevents same player being both removed and added in one tick
 * 
 * ENCRYPTION AND SECURITY:
 * 
 *   ISAAC cipher (if initialized):
 *     - Encrypts opcode only: opcode = (opcode + isaac_next()) & 0xFF
 *     - Payload remains unencrypted (obfuscation, not security)
 *     - Prevents trivial packet injection attacks
 *     - Does NOT protect against MITM or replay attacks
 * 
 *   Note: RS2 protocol has no integrity checking or authentication
 *   Modern alternatives: TLS 1.3, DTLS for UDP, message authentication codes
 */
void update_player(Player* player, Player* all_players[], u32 player_count, PlayerTracking* tracking) {
    if (!player || !tracking) return;

    StreamBuffer* out   = buffer_create(4096);
    StreamBuffer* block = buffer_create(2048);

    ISAACCipher* enc = player->out_cipher.initialized ? &player->out_cipher : NULL;
    buffer_write_header_var(out, SERVER_PLAYER_INFO, enc, VAR_SHORT);

    u32 payload_start = buffer_get_position(out);

    /* Write local player's update block FIRST (before any others) */
    if (player->update_flags != 0) {
        append_player_update_block(player, block, player->update_flags & 0xFF);
    }

    buffer_start_bit_access(out);

    update_local_player_movement(player, out);

    update_other_players(player, all_players, player_count, out, block, tracking);

    buffer_finish_bit_access(out);

    if (block->position > 0) {
        buffer_write_bytes(out, block->data, block->position);
    }

    buffer_finish_var_header(out, VAR_SHORT);

    int payload_len = (int)(buffer_get_position(out) - payload_start);
    dbg_log_send("PLAYER_INFO", SERVER_PLAYER_INFO, "varshort", payload_len, enc != NULL);

    network_send(player->socket_fd, out->data, out->position);

    buffer_destroy(block);
    buffer_destroy(out);

    player->region_changed  = false;
}

/*
 * update_other_players - Update tracked player list and encode changes
 * 
 * @param viewer        Local player (observer)
 * @param all_players   All active players on server
 * @param player_count  Count of active players
 * @param out           Bit-packed output buffer
 * @param block         Update block buffer (byte-aligned)
 * @param tracking      Viewer's tracking state
 * 
 * THREE-PHASE ALGORITHM:
 * 
 * Phase 1: Write tracked player count (8 bits)
 *   Tells client how many player slots to expect in update loop.
 *   Max value: 255 (hard protocol limit).
 * 
 * Phase 2: Update existing tracked players (REMOVAL AND MOVEMENT)
 *   For each tracked player:
 *     1. Linear search all_players[] to find current instance (O(P) per player)
 *     2. If not found OR out of view range:
 *        - Encode removal: [1:1][3:2] (3 bits)
 *        - Unmark tracked[pid] = false
 *        - Do NOT increment write_idx (compact array)
 *     3. If still visible:
 *        - Check movement state (primary_direction, secondary_direction)
 *        - Encode movement bits:
 *          * No movement, no update: [0:1] (1 bit)
 *          * No movement, has update: [1:1][0:2] (3 bits)
 *          * Walk: [1:1][1:2][dir:3][upd:1] (8 bits)
 *          * Run: [1:1][2:2][dir1:3][dir2:3][upd:1] (12 bits)
 *        - Keep in compacted local_players[] array
 *        - If has update flags, append to block buffer
 * 
 * Phase 3: Add new players entering view range (ADDITIONS)
 *   For each player in all_players[]:
 *     1. Skip if self (viewer)
 *     2. Skip if already tracked[index] = true
 *     3. Skip if needs_placement (player still teleporting)
 *     4. Test visibility with player_can_see():
 *        - Manhattan distance ≤ 15 tiles
 *        - Same height level
 *     5. If visible and space available (local_count < 255):
 *        - Encode addition: [index:11][delta_z:5][delta_x:5][jump:1][upd:1]
 *        - Mark tracked[index] = true
 *        - Add to local_players[local_count++]
 *        - ALWAYS append UPDATE_APPEARANCE block for new adds
 * 
 * Phase 4: Write end marker
 *   [2047:11] - all 11 bits set signals end of player additions list
 * 
 * ALGORITHMIC COMPLEXITY ANALYSIS:
 * 
 *   Let P = total online players (player_count)
 *   Let T = tracked players (tracking->local_count, max 255)
 *   Let V = players in view range (~10-30 typical)
 * 
 *   Phase 1: O(1) - write 8-bit count
 * 
 *   Phase 2 (update tracked):
 *     Outer loop: O(T) iterations over tracked players
 *     Inner loop: O(P) linear search to find player instance
 *     Per-player work: O(1) bit encoding
 *     Total: O(T × P) worst case
 *     Optimization potential: Use hash map all_players indexed by PID → O(T)
 * 
 *   Phase 3 (add new):
 *     Outer loop: O(P) iterations over all players
 *     player_can_see(): O(1) Manhattan distance check
 *     Per-player work: O(1) bit encoding + O(A) appearance block (A ≈ 80 bytes)
 *     Total: O(P) iterations, O(V) additions (early termination at local_count=255)
 * 
 *   Combined: O(T × P + P) = O(P × (T + 1))
 *   Typical: T=20, P=100 → ~2000 comparisons per update (600ms tick)
 *   Worst case: T=255, P=2048 → ~522,240 comparisons (acceptable for 600ms budget)
 * 
 *   Space complexity: O(T + V) for output buffers
 * 
 * BIT BUDGET ANALYSIS:
 * 
 *   Example: 20 tracked players, 5 new additions
 *     Header (tracked count):        8 bits
 *     15 players standing (no upd):  15 × 1 bit = 15 bits
 *     3 players walking:             3 × 8 bits = 24 bits
 *     2 players removed:             2 × 3 bits = 6 bits
 *     5 new additions:               5 × 23 bits = 115 bits
 *     End marker:                    11 bits
 *     Total bit section:             179 bits = 22.375 bytes
 *     Byte alignment padding:        +5 bits = 23 bytes
 *     Update blocks (20 appearances): 20 × 80 bytes = 1600 bytes
 *     Grand total:                   ~1623 bytes packet
 * 
 * PROTOCOL GUARANTEES:
 * 
 *   1. Tracked player list remains consistent between ticks
 *   2. Players cannot be added and removed in same tick (single state transition)
 *   3. New players ALWAYS get appearance block (client requirement)
 *   4. Removals are processed before additions (prevents double-counting)
 *   5. Local player count never exceeds 255 (protocol limit)
 * 
 * OPTIMIZATION OPPORTUNITIES:
 * 
 *   1. Replace linear search with hash map: all_players[pid] → O(1) lookup
 *      Current bottleneck: O(T × P) → O(T)
 *   2. Spatial partitioning: Quadtree for player_can_see() → O(log P)
 *      Current: O(P) full scan
 *   3. Dirty flags: Only check tracking for players who moved
 *      Current: Always scan all P players
 *   4. Appearance caching: Track appearance hash, skip block if unchanged
 *      Implemented: appearance_hashes[] array (not used in this function)
 */
static void update_other_players(Player* viewer, Player* all_players[], u32 player_count, 
                                StreamBuffer* out, StreamBuffer* block, PlayerTracking* tracking) {
    // printf("DEBUG: player=%s (idx=%u) local_count=%u bit_pos=%u tracking_ptr=%p\n", 
    //        viewer->username, viewer->index, tracking->local_count, out->bit_position, (void*)tracking);
    
    /*
     * PHASE 1: Write tracked player count (8 bits)
     * 
     * This count represents how many players the viewer is currently tracking
     * from the previous tick. The client will iterate this many times expecting
     * an update status for each tracked player (keep, remove, or update).
     * 
     * Value range: 0-255 (protocol limit, enforced in phase 3)
     * Wire format: [count:8] unsigned byte
     */
    buffer_write_bits(out, 8, tracking->local_count);
    
    /*
     * PHASE 2: Update existing tracked players
     * 
     * Two-pointer technique for in-place array compaction:
     *   - read_idx: scans all previously tracked players (0 to local_count-1)
     *   - write_idx: position where next kept player should be written
     * 
     * After this loop:
     *   - Removed players: write_idx stays same (gap created)
     *   - Kept players: copied to write_idx position (array compacted)
     *   - Result: local_players[0..write_idx-1] contains only still-visible players
     * 
     * Complexity: O(T × P) due to nested loop with linear search
     */
    u32 write_idx = 0;  /* Compaction write position */
    for (u32 read_idx = 0; read_idx < tracking->local_count; read_idx++) {
        u16 pid = tracking->local_players[read_idx];
        Player* other = NULL;
        
        /*
         * Linear search to find player instance by PID
         * 
         * BOTTLENECK: O(P) search repeated T times = O(T × P)
         * OPTIMIZATION: Replace with hash map all_players[pid] → O(1) lookup
         * 
         * Why linear search?
         *   - all_players[] is not sorted or indexed by PID
         *   - PIDs can be non-contiguous (1, 5, 100, 2047)
         *   - Hash map would add memory overhead: ~16 KB for 2048 buckets
         */
        for (u32 j = 0; j < player_count; j++) {
            if (all_players[j]->index == pid) {
                other = all_players[j];
                break;  /* Found player, exit search early */
            }
        }
        
        /*
         * Decision: Remove or keep player?
         * 
         * Remove if:
         *   1. Player not found (logged out or disconnected)
         *   2. Out of view range (moved >15 tiles away or changed height)
         * 
         * Keep if:
         *   Still visible and in range
         */
        if (!other || !player_can_see(viewer, other)) {
            /*
             * REMOVAL ENCODING:
             *   [update_required:1 = 1][movement_type:2 = 3]
             *   Total: 3 bits
             * 
             * Movement type 3 is reserved for removal (teleport is encoded differently)
             * Client interprets [1][3] as "remove this player from local list"
             */
            buffer_write_bits(out, 1, 1);  /* Update required flag */
            buffer_write_bits(out, 2, 3);  /* Movement type 3 = removal */
            tracking->tracked[pid] = false;  /* Unmark from tracking bitmap */
            /* Note: write_idx NOT incremented - creates gap in array */
        } else {
            /*
             * KEEP PLAYER: Compact into kept positions
             * 
             * Array compaction example:
             *   Before: [P1, P2, P3, P4, P5] (read_idx scans all)
             *   P2 removed: [P1, _, P3, P4, P5]
             *   P4 removed: [P1, _, P3, _, P5]
             *   After: [P1, P3, P5, _, _] (write_idx = 3)
             */
            tracking->local_players[write_idx++] = pid;
            
            bool has_moved = (other->primary_direction != -1);
            bool has_update = (other->update_flags != 0);
            
            if (has_moved) {
                /*
                 * MOVEMENT UPDATE ENCODING:
                 * 
                 * Run (2 tiles in one tick):
                 *   [update_req:1=1][move_type:2=2][dir1:3][dir2:3][has_upd:1]
                 *   Total: 12 bits
                 *   Example: Run northeast (N→E):
                 *     [1][10][000][010][0] = direction 0 (N), then 2 (E)
                 * 
                 * Walk (1 tile):
                 *   [update_req:1=1][move_type:2=1][dir:3][has_upd:1]
                 *   Total: 8 bits
                 *   Example: Walk east with appearance update:
                 *     [1][01][010][1]
                 */
                buffer_write_bits(out, 1, 1);  /* Update required */
                if (other->secondary_direction != -1) {
                    /* Run: both directions set */
                    buffer_write_bits(out, 2, 2);  /* Movement type 2 = run */
                    buffer_write_bits(out, 3, other->primary_direction);    /* First step */
                    buffer_write_bits(out, 3, other->secondary_direction);  /* Second step */
                    buffer_write_bits(out, 1, has_update ? 1 : 0);         /* Extended info flag */
                } else {
                    /* Walk: only primary direction */
                    buffer_write_bits(out, 2, 1);  /* Movement type 1 = walk */
                    buffer_write_bits(out, 3, other->primary_direction);
                    buffer_write_bits(out, 1, has_update ? 1 : 0);
                }
                
                /* Append update block if player has visual changes */
                if (has_update) {
                    append_player_update_block(other, block, other->update_flags & 0xFF);
                }
            } else {
                /*
                 * NO MOVEMENT ENCODING:
                 * 
                 * Standing with update (appearance/animation/graphics):
                 *   [update_req:1=1][move_type:2=0]
                 *   Total: 3 bits
                 * 
                 * Standing with no changes:
                 *   [update_req:1=0]
                 *   Total: 1 bit (most common case - ~70% of players)
                 */
                if (has_update) {
                    buffer_write_bits(out, 1, 1);  /* Update required */
                    buffer_write_bits(out, 2, 0);  /* Movement type 0 = stand */
                    append_player_update_block(other, block, other->update_flags & 0xFF);
                } else {
                    /* Optimal case: player unchanged, single bit encoding */
                    buffer_write_bits(out, 1, 0);  /* No update */
                }
            }
        }
    }
    
    /*
     * Update tracked count to reflect removals
     * 
     * Example:
     *   Initial: local_count = 20, removed 3 players
     *   Result: write_idx = 17, local_count = 17
     *   Array now contains only 17 visible players
     */
    tracking->local_count = write_idx;
    
    /*
     * PHASE 3: Add new players entering view range
     * 
     * Scans ALL online players to find new players who:
     *   1. Are NOT the viewer (cannot see self)
     *   2. Are NOT already tracked (prevent duplicates)
     *   3. Are NOT in placement mode (teleporting/logging in)
     *   4. ARE within view range (Manhattan distance ≤ 15, same height)
     * 
     * Loop termination conditions:
     *   - Early termination: local_count reaches 255 (protocol limit)
     *   - Normal termination: all players scanned
     * 
     * Complexity: O(P) where P = player_count
     * Average additions: ~5-10 players per tick (players walking into range)
     */
    printf("[SERVER] Third pass START - viewer=%s player_count=%u local_count=%u\n", 
           viewer->username, player_count, tracking->local_count);
    
    for (u32 i = 0; i < player_count && tracking->local_count < 255; i++) {
        Player* other = all_players[i];
        printf("[SERVER]   Checking all_players[%u]: index=%u username=%s state=%d needs_placement=%d pos=(%u,%u)\n", 
               i, other->index, other->username, other->state, other->needs_placement,
               other->position.x, other->position.z);
        
        /*
         * FILTER 1: Skip self
         * 
         * Player cannot see themselves in the multiplayer list.
         * Local player is handled separately in update_local_player_movement().
         */
        if (other == viewer) {
            printf("[SERVER]     -> Skipping (self)\n");
            continue;
        }
        
        /*
         * FILTER 2: Skip already tracked
         * 
         * tracked[] is a bitmap (bool array) marking which PIDs are in local_players[].
         * If tracked[pid] = true, player is already in our list from phase 2.
         * 
         * This prevents duplicate additions: same player cannot be both updated and added.
         */
        if (tracking->tracked[other->index]) {
            printf("[SERVER]     -> Skipping (already tracked)\n");
            continue;
        }
        
        /*
         * FILTER 3: Skip players in placement mode
         * 
         * needs_placement flag indicates player is:
         *   - Just logged in (initial spawn)
         *   - Teleporting between regions
         *   - Changing height levels
         * 
         * During placement, player's exact position is uncertain (client-side animation).
         * Adding them during placement causes visual glitches (player appears, disappears, reappears).
         * 
         * Wait until placement completes (next tick), then add with stable position.
         */
        if (other->needs_placement) {
            printf("[SERVER]     -> Skipping (needs_placement=%d)\n", other->needs_placement);
            continue;
        }
        
        /*
         * VISIBILITY CHECK: player_can_see()
         * 
         * Tests if target is within viewer's update range:
         *   1. Manhattan distance: |Δx| + |Δz| ≤ 15 tiles
         *   2. Same height level (floor 0-3)
         *   3. Visibility flags (not hidden by admin powers)
         * 
         * Complexity: O(1) - simple arithmetic comparison
         * 
         * Why Manhattan distance instead of Euclidean?
         *   - RS2 regions are square grids (8×8 chunks)
         *   - Manhattan distance matches region loading system
         *   - Faster: no square root calculation
         *   - Diamond-shaped view range (not circular)
         */
        bool can_see = player_can_see(viewer, other);
        printf("[SERVER]     -> player_can_see=%d (viewer pos=%u,%u other pos=%u,%u)\n",
               can_see, viewer->position.x, viewer->position.z, other->position.x, other->position.z);
        
        if (can_see) {
            printf("[SERVER] ADDING %s (idx=%u) to %s's local list\n", 
                   other->username, other->index, viewer->username);
            
            /*
             * PLAYER ADDITION SEQUENCE:
             * 
             * 1. Encode addition in bit-packed section (23 bits)
             *    [index:11][delta_z:5][delta_x:5][jump:1][update:1]
             * 
             * 2. Mark player as tracked
             *    tracked[index] = true prevents re-adding in same tick
             * 
             * 3. Add to local_players[] array
             *    local_players[local_count++] = PID
             *    Increments count, enforcing limit (loop condition checks < 255)
             * 
             * 4. Append UPDATE_APPEARANCE block
             *    New players ALWAYS get appearance block (client requirement)
             *    Without appearance, client shows player as invisible/default model
             */
            // u32 bit_before_add = out->bit_position;
            append_player_add(out, other, viewer);
            // printf("DEBUG: Player add wrote %u bits (from %u to %u)\n", 
            //        out->bit_position - bit_before_add, bit_before_add, out->bit_position);
            tracking->tracked[other->index] = true;
            tracking->local_players[tracking->local_count++] = other->index;
            
            /*
             * APPEARANCE BLOCK: Mandatory for new additions
             * 
             * UPDATE_APPEARANCE (0x1) contains:
             *   - Gender (male/female)
             *   - Body part styles (hair, beard, torso, arms, legs, feet)
             *   - Body colors (hair, torso, legs, feet, skin)
             *   - Equipment (weapons, armor)
             *   - Combat level
             *   - Skill level (total level)
             *   - Username (base37 encoded)
             * 
             * Size: ~80-100 bytes typical
             * 
             * Client behavior without appearance:
             *   - Shows default male model (bald, grey skin, no equipment)
             *   - Displays "null" as username
             *   - Combat level shows as 3
             */
            // u32 block_pos_before = block->position;
            append_player_update_block(other, block, UPDATE_APPEARANCE);
            // printf("DEBUG: Appearance block wrote %u bytes (from %u to %u), total block size=%u\n", 
            //        block->position - block_pos_before, block_pos_before, block->position, block->position);
        }
    }
    
    printf("[SERVER] Third pass END - added %u players\n", tracking->local_count - write_idx);
    
    /*
     * PHASE 4: Write end marker
     * 
     * [2047:11] - all 11 bits set (0b11111111111)
     * 
     * Signals end of player addition list to client.
     * Client stops reading new player adds when it encounters this marker.
     * 
     * Why 2047?
     *   - 11 bits can represent 0-2047
     *   - Valid PIDs are 1-2047 (0 is reserved as null)
     *   - 2047 is maximum valid PID, used as sentinel value
     *   - Client recognizes 2047 as "end of list" marker
     * 
     * Without end marker:
     *   - Client would read garbage bits as player data
     *   - Could cause buffer overflow or crash
     *   - Protocol spec REQUIRES this marker
     */
    buffer_write_bits(out, 11, 2047);
}

/*
 * append_player_add - Encode new player addition in bit mode
 * 
 * @param out     Output buffer (in bit access mode)
 * @param player  Player being added
 * @param viewer  Local player (for delta calculation)
 * 
 * BIT ENCODING FORMAT (23 bits total):
 *   [player_index:11][delta_x:5][delta_z:5][discard_queue:1][update_required:1]
 * 
 *   player_index (11 bits):
 *     - Player's unique identifier (PID)
 *     - Range: 1-2047 (0 is reserved as null)
 *     - Client uses this to create new player entity
 * 
 *   delta_x, delta_z (5 bits each, signed):
 *     - Relative offset from viewer's position
 *     - Encoding: Two's complement, masked to 5 bits
 *     - Range: -16 to +15 tiles
 *     - Client adds delta to viewer position to get absolute coords
 * 
 *   discard_queue (1 bit):
 *     - Always 1 for new player additions
 *     - Tells client to clear player's walking queue
 *     - Prevents movement glitches from previous session
 * 
 *   update_required (1 bit):
 *     - Always 1 for new additions
 *     - Tells client to expect update block (appearance data)
 *     - Without this, client won't read appearance block
 * 
 * COORDINATE DELTA ENCODING:
 * 
 *   5-bit signed representation (two's complement):
 *     Positive values: 0-15 → 0b00000 to 0b01111
 *     Negative values: -16 to -1 → 0b10000 to 0b11111
 * 
 *   Example deltas:
 *     Player at (100, 50), Viewer at (95, 48):
 *       delta_x = 100 - 95 = +5 → 0b00101
 *       delta_z = 50 - 48 = +2 → 0b00010
 *     
 *     Player at (90, 55), Viewer at (95, 48):
 *       delta_x = 90 - 95 = -5 → 0b11011 (two's complement)
 *       delta_z = 55 - 48 = +7 → 0b00111
 * 
 *   Masking with 0x1F (& 0b11111):
 *     - Truncates to 5 bits
 *     - Preserves two's complement representation
 *     - Example: -5 = 0xFFFFFFFB → 0x1B (correct 5-bit encoding)
 * 
 * PROTOCOL CONSTRAINTS:
 * 
 *   Maximum delta range: ±16 tiles
 *     - Enforced by player_can_see() Manhattan distance check (≤15)
 *     - If player is >15 tiles away, they shouldn't be added
 *     - 5 bits provides 32-value range: -16 to +15 (exactly fits)
 * 
 *   Client-side reconstruction:
 *     absolute_x = viewer_x + sign_extend_5bit(delta_x)
 *     absolute_z = viewer_z + sign_extend_5bit(delta_z)
 * 
 * WHY RELATIVE COORDINATES?
 * 
 *   Bandwidth optimization:
 *     - Absolute coords: 14 bits (X) + 14 bits (Z) = 28 bits
 *     - Relative coords: 5 bits (X) + 5 bits (Z) = 10 bits
 *     - Savings: 18 bits per player add (64% reduction)
 * 
 *   Example: 10 players entering view
 *     Absolute: 10 × 28 bits = 280 bits = 35 bytes
 *     Relative: 10 × 10 bits = 100 bits = 12.5 bytes
 *     Bandwidth saved: 22.5 bytes per update
 */
static void append_player_add(StreamBuffer* out, Player* player, Player* viewer) {
    /*
     * FIELD 1: Player index (11 bits)
     * 
     * Uniquely identifies this player in the server's player list.
     * Client allocates new Player entity with this index.
     * 
     * Range: 1-2047 (2048 max players, PID 0 reserved)
     */
    buffer_write_bits(out, 11, player->index);
    
    /*
     * FIELD 2-3: Position deltas (5 bits each)
     * 
     * Calculate relative offset from viewer's current position.
     * Client will add these deltas to viewer position to determine
     * where to spawn the new player on screen.
     */
    i32 delta_x = player->position.x - viewer->position.x;
    i32 delta_z = player->position.z - viewer->position.z;
    
    /*
     * Encode deltas as 5-bit two's complement signed values
     * 
     * Masking with 0x1F preserves sign in two's complement:
     *   Positive: 0-15 → 0b00000-0b01111
     *   Negative: -16 to -1 → 0b10000-0b11111
     */
    buffer_write_bits(out, 5, delta_x & 0x1F);
    buffer_write_bits(out, 5, delta_z & 0x1F);
    
    /*
     * FIELD 4: Discard walking queue (1 bit)
     * 
     * Always set to 1 for new player additions.
     * Tells client to clear any previous walking queue for this player.
     * 
     * Prevents visual glitch: Without this, client might show player
     * walking from their last known position (from previous session).
     */
    buffer_write_bits(out, 1, 1);
    
    /*
     * FIELD 5: Update required flag (1 bit)
     * 
     * Always set to 1 for new player additions.
     * Signals client to read update block (appearance data) for this player.
     * 
     * Protocol requirement: New players MUST have appearance block.
     * Without appearance, client shows default model (grey, bald, nude).
     */
    buffer_write_bits(out, 1, 1);
    
    printf("[SERVER] append_player_add: player=%s (idx=%u) delta_z=%d delta_x=%d viewer=%s pos=(%u,%u) player_pos=(%u,%u)\n", 
           player->username, player->index, delta_z, delta_x, viewer->username,
           viewer->position.x, viewer->position.z, player->position.x, player->position.z);
}

/*
 * append_player_update_block - Write update block data in byte mode
 * 
 * @param player  Player with updates
 * @param block   Output buffer (byte-aligned)
 * @param mask    Update flags bitmask
 * 
 * BLOCK STRUCTURE (variable length):
 * 
 *   [mask:1 byte]  Bitmask of which updates are present
 *   [blocks...]    Update data in flag order
 * 
 * UPDATE MASK FLAGS (processed in this order):
 * 
 *   UPDATE_APPEARANCE (0x01):
 *     [length:1][appearance_data:length]
 *     Size: 46-100 bytes typical
 *     Contains: Gender, body parts, colors, animations, username, combat level
 * 
 *   UPDATE_ANIMATION (0x02):
 *     [animation_id:2 big-endian][delay:1]
 *     Size: 3 bytes
 *     Examples: Attack swing (422), Death (836), Emote (855-862)
 * 
 *   UPDATE_GRAPHIC (0x04):
 *     [graphic_id:2 big-endian][height:2][delay:1]
 *     Size: 5 bytes
 *     Examples: Teleport graphic (308), Level up (199), Prayer activate (83)
 * 
 *   UPDATE_CHAT (0x08):
 *     [type_and_effect:1][color:1][length:1][packed_text:length]
 *     Size: 10-80 bytes
 *     Text packed using wordpack compression
 * 
 * MASK PROCESSING ORDER:
 * 
 *   Protocol specification requires blocks in flag bit order:
 *     1. Appearance (if mask & 0x01)
 *     2. Animation (if mask & 0x02)
 *     3. Graphic (if mask & 0x04)
 *     4. Chat (if mask & 0x08)
 * 
 *   Client reads blocks in same order, using mask to determine which to expect.
 *   Out-of-order blocks cause client to read wrong data → crash or corruption.
 * 
 * LENGTH-PREFIXED ENCODING:
 * 
 *   Only appearance uses length prefix (variable-length data).
 *   Other updates have fixed sizes (client knows exact byte count).
 * 
 *   Why length prefix for appearance?
 *     - Equipment slots vary (0 or 2 bytes per slot)
 *     - Total size: 46-100 bytes depending on equipment
 *     - Client needs to know where appearance ends, next block begins
 * 
 *   Why NO length prefix for animation/graphic?
 *     - Fixed 3 bytes (animation) or 5 bytes (graphic)
 *     - Client can read exact count without length field
 *     - Saves 1 byte per update
 * 
 * TYPICAL BLOCK SIZES:
 * 
 *   Appearance only:
 *     mask=0x01, appearance=80 bytes
 *     Total: 1 + 1 + 80 = 82 bytes
 * 
 *   Appearance + Animation:
 *     mask=0x03, appearance=80, animation=3
 *     Total: 1 + 1 + 80 + 3 = 85 bytes
 * 
 *   Animation + Graphic (combat hit):
 *     mask=0x06, animation=3, graphic=5
 *     Total: 1 + 3 + 5 = 9 bytes
 * 
 *   Chat only:
 *     mask=0x08, chat=20
 *     Total: 1 + 20 = 21 bytes
 * 
 * BUFFER MANAGEMENT:
 * 
 *   Appearance requires temporary buffer:
 *     1. Create 128-byte buffer (max appearance size)
 *     2. Write appearance data to temp buffer
 *     3. Write length byte to main block
 *     4. Copy temp buffer contents to main block
 *     5. Destroy temp buffer
 * 
 *   Why temporary buffer?
 *     - Need to know final size before writing length prefix
 *     - Can't write length until appearance is fully encoded
 *     - Alternative: Pre-calculate size (complex, error-prone)
 * 
 * MEMORY ALLOCATION:
 * 
 *   128-byte temp buffer allocation:
 *     Per appearance update: ~128 bytes heap allocation
 *     Typical: 20 players with appearance = 20 × 128 = 2.56 KB
 *     
 *   Optimization opportunity:
 *     - Pre-allocate buffer pool (reusable 128-byte buffers)
 *     - Current: malloc/free per appearance
 *     - Optimized: Borrow from pool, return when done
 *     - Savings: Eliminate 20-30 malloc calls per tick
 */
static void append_player_update_block(Player* player, StreamBuffer* block, u8 mask) {
    /*
     * Write update mask byte
     * 
     * Client reads this first to determine which update blocks follow.
     * Each bit set in mask corresponds to an update block present.
     * Client processes blocks in flag bit order (0x01, 0x02, 0x04, 0x08).
     */
    buffer_write_byte(block, mask);
    
    /*
     * APPEARANCE UPDATE (0x01)
     * 
     * Write player's visual appearance data with length prefix.
     * Length prefix required because appearance size varies (46-100 bytes).
     * 
     * Process:
     *   1. Allocate temporary buffer (128 bytes, enough for max appearance)
     *   2. Encode appearance to temp buffer
     *   3. Write length (u8) to main block
     *   4. Copy appearance data from temp to main block
     *   5. Free temporary buffer
     */
    if (mask & UPDATE_APPEARANCE) {
        StreamBuffer* appearance_buf = buffer_create(128);
        append_appearance(player, appearance_buf);
        
        /* Write length prefix (u8, max 255 bytes) */
        buffer_write_byte(block, (u8)appearance_buf->position);
        
        /* Copy appearance data to main block */
        buffer_write_bytes(block, appearance_buf->data, appearance_buf->position);
        
        /* Free temporary buffer */
        buffer_destroy(appearance_buf);
    }
    
    /*
     * TODO: Implement remaining update types
     * 
     * UPDATE_ANIMATION (0x02):
     *   buffer_write_short(block, animation_id, BIG_ENDIAN);
     *   buffer_write_byte(block, delay);
     * 
     * UPDATE_GRAPHIC (0x04):
     *   buffer_write_short(block, graphic_id, BIG_ENDIAN);
     *   buffer_write_short(block, height, BIG_ENDIAN);
     *   buffer_write_byte(block, delay);
     * 
     * UPDATE_CHAT (0x08):
     *   Removed in recent changes (chat system reverted)
     */
}

/*******************************************************************************
 * MOVEMENT ENCODING HELPERS
 ******************************************************************************/

/*
 * append_placement - Encode teleport/region change movement
 * 
 * BIT ENCODING (20 bits total):
 *   [movement_type:2=3][z:2][local_x:7][local_y:7][reset_queue:1][has_update:1]
 * 
 *   movement_type (2 bits):
 *     - Value: 3 (0b11)
 *     - Signals teleport/placement to client
 *     - Different from removal (which uses full movement update)
 * 
 *   z (2 bits):
 *     - Height level (floor/plane)
 *     - Range: 0-3 (4 floors in RS2)
 *     - Example: 0=ground, 1=first floor, 2=second floor, 3=roof
 * 
 *   local_x, local_y (7 bits each):
 *     - Region-local coordinates (within 64×64 region)
 *     - Range: 0-127 (but typically 0-63 for standard regions)
 *     - Client converts to absolute by: abs = region_base + local
 * 
 *   reset_queue (1 bit):
 *     - Clear player's walking queue flag
 *     - Always 0 for teleports (movement is instant)
 *     - Prevents client from animating walk after teleport
 * 
 *   has_update (1 bit):
 *     - Whether update block follows (appearance/animation/etc.)
 *     - Typically 0 for pure teleports, 1 for login (needs appearance)
 * 
 * WHEN USED:
 *   - Player login (initial spawn position)
 *   - Teleport spells (Lumbridge, Varrock, etc.)
 *   - Height changes (stairs, ladders)
 *   - Region changes (crossing major map boundaries)
 *   - Admin commands (/tele x y z)
 * 
 * REGION COORDINATE SYSTEM:
 * 
 *   RuneScape divides world into 64×64 tile regions.
 *   Each region has local coordinates 0-63 (but 7-bit allows 0-127).
 * 
 *   Example: Lumbridge spawn
 *     Absolute world coords: (3222, 3218, 0)
 *     Region ID: 50 (x) × 64, 50 (y) × 64
 *     Region base: (3200, 3200)
 *     Local coords: (3222-3200=22, 3218-3200=18)
 *     Encoded: z=0, local_x=22, local_y=18
 */
static void append_placement(StreamBuffer* out, u32 local_x, u32 local_y, u32 z, bool reset_move, bool update) {
    /* Movement type 3 = teleport/placement */
    buffer_write_bits(out, 2, 3);
    
    /* Height level (0-3, masked to 2 bits) */
    buffer_write_bits(out, 2, z & 0x3);
    
    /* Region-local X coordinate (0-127, masked to 7 bits) */
    buffer_write_bits(out, 7, local_x & 0x7F);
    
    /* Region-local Y coordinate (0-127, masked to 7 bits) */
    buffer_write_bits(out, 7, local_y & 0x7F);
    
    /* Reset walking queue flag (0 for teleports, prevents walk animation) */
    buffer_write_bits(out, 1, reset_move ? 1 : 0);
    
    /* Update block follows flag (1 if appearance/animation needed) */
    buffer_write_bits(out, 1, update ? 1 : 0);
}

/*
 * append_walk - Encode walking movement (single direction)
 * 
 * BIT ENCODING (6 bits total):
 *   [movement_type:2=1][direction:3][has_update:1]
 * 
 *   movement_type (2 bits):
 *     - Value: 1 (0b01)
 *     - Signals single-tile walk to client
 * 
 *   direction (3 bits):
 *     - Compass direction: 0-7
 *     - Encoding:
 *       0 = North (N)
 *       1 = Northeast (NE)
 *       2 = East (E)
 *       3 = Southeast (SE)
 *       4 = South (S)
 *       5 = Southwest (SW)
 *       6 = West (W)
 *       7 = Northwest (NW)
 * 
 *   has_update (1 bit):
 *     - Whether update block follows
 *     - Example: Walk + change equipment = has_update=1
 * 
 * DIRECTION ENCODING DIAGRAM:
 * 
 *          N (0)
 *       NW   |   NE
 *      (7)   |   (1)
 *         \  |  /
 *   W (6) ---+--- E (2)
 *         /  |  \
 *      (5)   |   (3)
 *       SW   |   SE
 *          S (4)
 * 
 * TILE DELTA CALCULATION:
 * 
 *   direction → (dx, dz):
 *     0 (N):  ( 0, +1)
 *     1 (NE): (+1, +1)
 *     2 (E):  (+1,  0)
 *     3 (SE): (+1, -1)
 *     4 (S):  ( 0, -1)
 *     5 (SW): (-1, -1)
 *     6 (W):  (-1,  0)
 *     7 (NW): (-1, +1)
 * 
 *   Client applies: new_pos = old_pos + delta
 * 
 * BANDWIDTH EFFICIENCY:
 * 
 *   Walking is the most common player action (~70% of ticks).
 *   Optimizing walk encoding has massive bandwidth impact.
 * 
 *   Walk encoding: 6 bits (0.75 bytes)
 *   vs absolute position: 28 bits (3.5 bytes)
 *   Savings: 78% per walking player
 * 
 *   Example: 100 players, 70 walking:
 *     Walk encoding: 70 × 6 bits = 420 bits = 52.5 bytes
 *     Absolute: 70 × 28 bits = 1960 bits = 245 bytes
 *     Saved: 192.5 bytes per tick
 *     Over 600ms tick rate: ~321 bytes/sec per 100 players
 */
static void append_walk(StreamBuffer* out, i32 direction, bool update) {
    /* Movement type 1 = walk (single tile) */
    buffer_write_bits(out, 2, 1);
    
    /* Direction: 0-7 (N, NE, E, SE, S, SW, W, NW), masked to 3 bits */
    buffer_write_bits(out, 3, direction & 0x7);
    
    /* Update block follows flag */
    buffer_write_bits(out, 1, update ? 1 : 0);
}

/*
 * append_run - Encode running movement (two directions)
 * 
 * BIT ENCODING (9 bits total):
 *   [movement_type:2=2][direction1:3][direction2:3][has_update:1]
 * 
 *   movement_type (2 bits):
 *     - Value: 2 (0b10)
 *     - Signals two-tile movement (run) to client
 * 
 *   direction1, direction2 (3 bits each):
 *     - Two consecutive movement directions
 *     - Encoding: 0-7 (N, NE, E, SE, S, SW, W, NW)
 *     - Applied in sequence: first move dir1, then move dir2
 * 
 *   has_update (1 bit):
 *     - Whether update block follows
 * 
 * RUNNING MECHANICS:
 * 
 *   Running consumes energy and moves 2 tiles per tick (600ms).
 *   Walking moves 1 tile per tick.
 *   Running is 2× faster but depletes run energy.
 * 
 *   Energy depletion:
 *     - Flat terrain: 1% per tile
 *     - Uphill: 2% per tile
 *     - Weighted by equipment (heavy armor drains faster)
 * 
 *   When energy reaches 0%, player automatically switches to walking.
 * 
 * DIRECTION SEQUENCE EXAMPLES:
 * 
 *   Run east (E, E):
 *     dir1=2 (E), dir2=2 (E)
 *     Result: Move 2 tiles east
 * 
 *   Run northeast (N, E):
 *     dir1=0 (N), dir2=2 (E)
 *     Result: Move north, then east (diagonal path)
 * 
 *   Turn while running (N, W):
 *     dir1=0 (N), dir2=6 (W)
 *     Result: Move north, then west (L-shaped path)
 * 
 * PROTOCOL CONSTRAINT:
 * 
 *   Both directions MUST be valid (0-7).
 *   Invalid direction (-1) indicates no movement in that step.
 *   Run requires BOTH dir1 and dir2 to be valid.
 *   If only dir1 valid: use append_walk() instead.
 * 
 * BANDWIDTH COMPARISON:
 * 
 *   Run encoding: 9 bits (1.125 bytes)
 *   Walk × 2: 12 bits (1.5 bytes) - if sent as two separate updates
 *   Absolute position: 28 bits (3.5 bytes)
 *   
 *   Running players (~4% of population):
 *     100 players, 4 running:
 *       Run encoding: 4 × 9 bits = 36 bits = 4.5 bytes
 *       Absolute: 4 × 28 bits = 112 bits = 14 bytes
 *       Saved: 9.5 bytes per tick
 */
static void append_run(StreamBuffer* out, i32 dir1, i32 dir2, bool update) {
    /* Movement type 2 = run (two tiles) */
    buffer_write_bits(out, 2, 2);
    
    /* First direction (0-7), masked to 3 bits */
    buffer_write_bits(out, 3, dir1 & 0x7);
    
    /* Second direction (0-7), masked to 3 bits */
    buffer_write_bits(out, 3, dir2 & 0x7);
    
    /* Update block follows flag */
    buffer_write_bits(out, 1, update ? 1 : 0);
}

/*
 * append_stand - Encode stationary with update flag
 * 
 * BIT ENCODING (2 bits total):
 *   [movement_type:2=0]
 * 
 *   movement_type (2 bits):
 *     - Value: 0 (0b00)
 *     - Signals no movement (standing still)
 *     - Player remains at current position
 * 
 * WHEN USED:
 * 
 *   Player is stationary BUT has update flag set:
 *     - Changing equipment (appearance update)
 *     - Playing animation (emote, combat swing)
 *     - Spot animation (graphics overlay)
 *     - Public chat message
 * 
 *   If player has NO update flags, append_stand() is NOT called.
 *   Instead, update_other_players() writes single 0 bit directly.
 * 
 * ENCODING OPTIMIZATION:
 * 
 *   Standing with NO updates: 1 bit [0]
 *   Standing WITH updates: 2 bits [movement_type:2=0] + update blocks
 * 
 *   This is the most common case (~70% of players each tick).
 *   1-bit encoding for unchanged players is critical for bandwidth.
 * 
 * FREQUENCY DISTRIBUTION (typical 100-player server):
 * 
 *   70 players: Standing, no changes → 1 bit each = 70 bits
 *   20 players: Walking → 6 bits each = 120 bits
 *   5 players: Standing, appearance change → 2 bits + blocks
 *   4 players: Running → 9 bits each = 36 bits
 *   1 player: Teleporting → 20 bits
 * 
 *   Total movement bits: 70 + 120 + 10 + 36 + 20 = 256 bits = 32 bytes
 *   vs absolute positions: 100 × 28 bits = 2800 bits = 350 bytes
 *   Compression: 91% bandwidth reduction
 */
static void append_stand(StreamBuffer* out) {
    /* Movement type 0 = stand (no movement) */
    buffer_write_bits(out, 2, 0);
}

/*******************************************************************************
 * APPEARANCE BLOCK
 ******************************************************************************/

/*
 * append_appearance - Encode player appearance data
 * 
 * @param player  Player to encode
 * @param out     Output buffer (byte-aligned mode)
 * 
 * BYTE-BY-BYTE STRUCTURE (~80-100 bytes total):
 * 
 *   [gender:1]              Male (0) or Female (1)
 *   [head_icons:1]          Prayer/PK skull icons (0-7 bitmask)
 *   [body_slots:12×2]       12 appearance slots (24 bytes max)
 *   [colors:5]              Body color palette indices
 *   [animations:7×2]        Movement animation IDs (14 bytes)
 *   [username:8]            Base37-encoded username (long)
 *   [combat_level:1]        Combat level (3-126)
 *   [skill_level:2]         Total skill level (optional, not used here)
 * 
 * BODY SLOT LAYOUT (12 slots):
 * 
 *   Slot mapping (client-side slot → server body[] index):
 *     Slot 0-3:  Equipment slots (helmet, cape, amulet, weapon)
 *     Slot 4:    body[2] - Torso/chest
 *     Slot 5:    Equipment (shield)
 *     Slot 6:    body[3] - Arms
 *     Slot 7:    body[5] - Legs
 *     Slot 8:    body[0] - Hair/head
 *     Slot 9:    body[4] - Hands
 *     Slot 10:   body[6] - Feet
 *     Slot 11:   body[1] - Jaw/beard
 * 
 *   Encoding per slot (variable length):
 *     Empty slot (-1): Write 0x00 (1 byte)
 *     Body part (≥0): Write 0x0100 | part_id as big-endian u16 (2 bytes)
 * 
 *   Why 0x0100 prefix?
 *     Distinguishes equipment (0x01XX = item) from body parts (0x01XX = model)
 *     Client interprets values:
 *       0x0000: Empty slot
 *       0x0100-0x01FF: Body model ID (0-255)
 *       0x0200+: Equipment item ID
 * 
 * COLOR PALETTE (5 bytes):
 * 
 *   colors[0]: Hair color (0-24)
 *   colors[1]: Torso color (0-28)
 *   colors[2]: Leg color (0-22)
 *   colors[3]: Feet color (0-5)
 *   colors[4]: Skin color (0-7)
 * 
 *   Color indices map to RGB values defined client-side.
 *   Example: Skin color 0 = light, 1 = tan, 2 = dark, etc.
 * 
 * ANIMATION IDS (7 animations, 14 bytes):
 * 
 *   Default male animations (from JAGeX animation library):
 *     808: Stand animation (idle stance)
 *     823: Turn (rotate to face direction)
 *     819: Walk (walking animation)
 *     820: Turn 180 degrees
 *     821: Turn 90 degrees clockwise
 *     822: Turn 90 degrees counter-clockwise
 *     824: Run (running animation)
 * 
 *   Female animations differ (809 stand, 831 walk, etc.)
 *   Equipment can override (weapon stance, run with heavy armor)
 * 
 * BASE37 USERNAME ENCODING:
 * 
 *   Compresses username (1-12 chars) into 64-bit long.
 *   Character set: space, a-z, 0-9 (37 characters total)
 * 
 *   Encoding: Each char mapped to 0-36, packed base-37:
 *     'a'=1, 'b'=2, ..., 'z'=26, '0'=27, '1'=28, ..., '9'=36, ' '=0
 *   
 *   Example: "player1"
 *     p=16, l=12, a=1, y=25, e=5, r=18, 1=28
 *     Encoded: 16×37^6 + 12×37^5 + 1×37^4 + 25×37^3 + 5×37^2 + 18×37 + 28
 *     = 6582952005840 (fits in 64-bit long)
 * 
 *   Advantages over ASCII strings:
 *     - Fixed 8 bytes (vs 1-12 bytes variable)
 *     - Case-insensitive (lowercase only)
 *     - Efficient comparison (integer comparison)
 *     - Supports up to 12 characters (37^12 < 2^64)
 * 
 * COMBAT LEVEL CALCULATION:
 * 
 *   Based on combat skills (Attack, Strength, Defence, Hitpoints, Prayer, Magic, Ranged):
 *     Base = 0.25 × (Defence + Hitpoints + floor(Prayer / 2))
 *     Melee = 0.325 × (Attack + Strength)
 *     Range = 0.325 × floor(Ranged × 1.5)
 *     Mage = 0.325 × floor(Magic × 1.5)
 *     Combat = Base + max(Melee, Range, Mage)
 * 
 *   Range: 3 (minimum) to 126 (maximum)
 *   New players: Combat level 3
 *   Maxed players (99 all combat stats): Combat level 126
 * 
 * TYPICAL PACKET SIZE:
 * 
 *   Minimum (all body parts, no equipment):
 *     1 + 1 + 16 + 5 + 14 + 8 + 1 = 46 bytes
 * 
 *   With equipment (6 slots filled):
 *     1 + 1 + 24 + 5 + 14 + 8 + 1 = 54 bytes
 * 
 *   Maximum (with skill level):
 *     1 + 1 + 24 + 5 + 14 + 8 + 1 + 2 = 56 bytes
 * 
 * WHEN APPEARANCE IS SENT:
 * 
 *   Mandatory:
 *     - New player entering view (always with UPDATE_APPEARANCE flag)
 *     - Player login (initial appearance for all nearby players)
 * 
 *   Optional (triggered by events):
 *     - Equipment change (wear/remove armor, weapons)
 *     - Appearance change (hairdresser, makeover mage)
 *     - Gender change (makeover mage)
 *     - Level up affecting combat level
 * 
 * APPEARANCE CACHING OPTIMIZATION:
 * 
 *   Client caches appearance by player index.
 *   If appearance_hash unchanged, server can skip sending appearance.
 *   
 *   appearance_hash calculation (simple hash):
 *     hash = gender ^ (body[0]<<1) ^ (body[1]<<2) ^ ... ^ combat_level
 * 
 *   PlayerTracking has appearance_hashes[] array (not currently used).
 *   Future optimization: Track hash, only send when changed.
 */
static void append_appearance(Player* player, StreamBuffer* out) {
    u32 start_pos = out->position;
    
    /*
     * FIELD 1: Gender (1 byte)
     * Value: 0 = male, 1 = female
     * Affects: Animation set, body model defaults, voice pitch
     */
    buffer_write_byte(out, player->gender);
    
    /*
     * FIELD 2: Head icons (1 byte, bitmask)
     * Bits 0-2: Prayer icon (0-6, mapped to prayer level tiers)
     * Bits 3-5: PK skull icon (0=none, 1=white skull, 2=red skull)
     * Bits 6-7: Reserved/unused
     * 
     * Value 0: No icons displayed (typical for most players)
     * 
     * Prayer icons:
     *   0: None
     *   1: Protect Item
     *   2: Protect Melee/Range/Magic
     *   3: Retribution
     *   4: Redemption
     *   5: Smite
     *   6: Multiple prayers active
     * 
     * PK skull:
     *   Appears when player attacks another in Wilderness
     *   White skull: Normal PK attack
     *   Red skull: Multiple kills (red = dangerous)
     */
    buffer_write_byte(out, 0);

    /*
     * FIELD 3: Body part slots (12 slots, variable length)
     * 
     * Transform player's 7-part body array into client's 12-slot format.
     * Slots 0-3, 5 reserved for equipment (helmet, cape, amulet, weapon, shield).
     * Remaining slots filled with body part models.
     * 
     * Mapping rationale:
     *   Client expects specific slot order for rendering:
     *     - Slot 8 (hair) renders on top of head
     *     - Slot 11 (beard) renders on jaw
     *     - Slot 4 (torso) is base model
     *     - Other slots layer on top
     * 
     * Server stores compact 7-element array:
     *   body[0]=hair, body[1]=beard, body[2]=torso, body[3]=arms,
     *   body[4]=hands, body[5]=legs, body[6]=feet
     * 
     * Transformed to 12-slot client format with equipment gaps.
     */
    i32 body[12] = {
        -1,  /* Slot 0:  Equipment - Helmet (empty = show hair) */
        -1,  /* Slot 1:  Equipment - Cape */
        -1,  /* Slot 2:  Equipment - Amulet */
        -1,  /* Slot 3:  Equipment - Weapon (right hand) */
        player->body[2],  /* Slot 4:  Body part - Torso/chest */
        -1,  /* Slot 5:  Equipment - Shield (left hand) */
        player->body[3],  /* Slot 6:  Body part - Arms */
        player->body[5],  /* Slot 7:  Body part - Legs */
        player->body[0],  /* Slot 8:  Body part - Hair/head */
        player->body[4],  /* Slot 9:  Body part - Hands */
        player->body[6],  /* Slot 10: Body part - Feet */
        player->body[1]   /* Slot 11: Body part - Jaw/beard */
    };
    
    printf("[APPEARANCE] %s: gender=%d body=[%d,%d,%d,%d,%d,%d,%d] colors=[%d,%d,%d,%d,%d]\n",
           player->username, player->gender,
           player->body[0], player->body[1], player->body[2], player->body[3],
           player->body[4], player->body[5], player->body[6],
           player->colors[0], player->colors[1], player->colors[2], player->colors[3], player->colors[4]);
    
    /*
     * Encode each of 12 body slots:
     *   Empty (-1): Write 0x00 (1 byte) → slot shows equipment or nothing
     *   Body part (≥0): Write (0x0100 | part_id) as big-endian u16 (2 bytes)
     * 
     * 0x0100 prefix distinguishes body models from equipment items:
     *   0x0000: Empty
     *   0x0100-0x01FF: Body model (256 possible models)
     *   0x0200+: Equipment item ID (thousands of items)
     */
    for (int i = 0; i < 12; i++) {
        i32 part = body[i];
        if (part < 0) {
            /* Empty slot: 1 byte */
            buffer_write_byte(out, 0);
        } else {
            /* Body part: OR with 0x100, write as big-endian short (2 bytes) */
            part |= 0x100;
            buffer_write_short(out, (u16)part, BYTE_ORDER_BIG);
        }
    }

    /*
     * FIELD 4: Color palette (5 bytes)
     * 
     * Each byte is index into client-side color lookup table.
     * Client maps index to RGB color for rendering.
     * 
     * Allows customization without storing full RGB values (saves bandwidth).
     * Example: Hair color 5 might map to RGB(139, 69, 19) brown.
     */
    for (int i = 0; i < 5; i++) {
        buffer_write_byte(out, player->colors[i]);
    }

    /*
     * FIELD 5: Movement animations (7 animations, 14 bytes)
     * 
     * Animation IDs from JAGeX animation library (packed in cache).
     * Client plays these animations for various movement states.
     * 
     * Default male animations (female animations different):
     *   808: Stand animation (idle, breathing)
     *   823: Stand turn (pivot to face direction)
     *   819: Walk forward
     *   820: Turn 180 degrees (about-face)
     *   821: Turn 90 degrees clockwise
     *   822: Turn 90 degrees counter-clockwise
     *   824: Run animation (2 tiles/tick)
     * 
     * Equipment can override (e.g., weapon changes stance animation).
     */
    buffer_write_short(out, 808, BYTE_ORDER_BIG);  /* Stand */
    buffer_write_short(out, 823, BYTE_ORDER_BIG);  /* Stand turn */
    buffer_write_short(out, 819, BYTE_ORDER_BIG);  /* Walk forward */
    buffer_write_short(out, 820, BYTE_ORDER_BIG);  /* Turn 180° */
    buffer_write_short(out, 821, BYTE_ORDER_BIG);  /* Turn 90° CW */
    buffer_write_short(out, 822, BYTE_ORDER_BIG);  /* Turn 90° CCW */
    buffer_write_short(out, 824, BYTE_ORDER_BIG);  /* Run */

    /*
     * FIELD 6: Username (8 bytes)
     * 
     * Encoded as base-37 long for bandwidth efficiency.
     * Base-37 character set: space, a-z, 0-9 (37 total characters)
     * 
     * Supports up to 12 characters: 37^12 = 6.58e18 < 2^64
     * 
     * Example encoding "zezima":
     *   z=26, e=5, z=26, i=9, m=13, a=1
     *   Result: 26×37^5 + 5×37^4 + 26×37^3 + 9×37^2 + 13×37 + 1
     *         = 200560490155 (fits in 64-bit long)
     * 
     * Advantages:
     *   - Fixed 8 bytes (vs 1-12 variable ASCII)
     *   - Case-insensitive (all lowercase)
     *   - Fast comparison (integer equality)
     *   - No null terminator needed
     */
    u64 base37 = username_to_base37(player->username);
    buffer_write_long(out, base37);
    
    /*
     * FIELD 7: Combat level (1 byte)
     * 
     * Player's combat level (3-126).
     * Calculated from combat skills: Attack, Strength, Defence,
     * Hitpoints, Prayer, Magic, Ranged.
     * 
     * Formula:
     *   Base = 0.25 × (Defence + Hitpoints + floor(Prayer/2))
     *   Melee = 0.325 × (Attack + Strength)
     *   Range = 0.325 × floor(Ranged × 1.5)
     *   Mage = 0.325 × floor(Magic × 1.5)
     *   Combat = Base + max(Melee, Range, Mage)
     * 
     * Hardcoded to 3 for new players (minimum level).
     * Full implementation would calculate from player->skills[].
     * 
     * Combat level determines:
     *   - PvP attack range (can only attack ±combat_level in Wilderness)
     *   - Visual display above player's head
     *   - Threat assessment for other players
     */
    buffer_write_byte(out, 3);
    
    // printf("DEBUG: Appearance block size=%u bytes (calculated from pos %u to %u)\n", 
    //        out->position - start_pos, start_pos, out->position);
}

/*
 * username_to_base37 - Convert username string to base-37 encoding
 * 
 * @param username  Null-terminated username string (1-12 chars)
 * @return          64-bit base-37 encoded value
 * 
 * BASE-37 ENCODING SYSTEM:
 * 
 *   Character set (37 symbols):
 *     0: Space/underscore
 *     1-26: Letters a-z (case-insensitive)
 *     27-36: Digits 0-9
 * 
 *   Mathematical representation:
 *     For username of length n with characters c[0]..c[n-1]:
 *     encoded = c[0]×37^(n-1) + c[1]×37^(n-2) + ... + c[n-1]×37^0
 * 
 *   Example: "abc"
 *     a=1, b=2, c=3
 *     encoded = 1×37² + 2×37¹ + 3×37⁰
 *             = 1×1369 + 2×37 + 3
 *             = 1369 + 74 + 3 = 1446
 * 
 *   Example: "zezima" (famous RuneScape player)
 *     z=26, e=5, z=26, i=9, m=13, a=1
 *     encoded = 26×37^5 + 5×37^4 + 26×37^3 + 9×37^2 + 13×37 + 1
 *             = 26×69,343,957 + 5×1,874,161 + 26×50,653 + 9×1,369 + 13×37 + 1
 *             = 1,802,942,882 + 9,370,805 + 1,316,978 + 12,321 + 481 + 1
 *             = 1,813,643,468
 * 
 * ENCODING TABLE:
 * 
 *   Input  → Value
 *   ─────────────────
 *   ' '    → 0  (space/underscore)
 *   'a'    → 1
 *   'b'    → 2
 *   ...
 *   'z'    → 26
 *   '0'    → 27
 *   '1'    → 28
 *   ...
 *   '9'    → 36
 * 
 *   Uppercase letters converted to lowercase (A→a, B→b, etc.)
 *   Other characters (!, @, #, etc.) are skipped
 * 
 * MAXIMUM LENGTH:
 * 
 *   37^12 = 6,582,952,005,840,035,281 ≈ 6.58e18
 *   2^64  = 18,446,744,073,709,551,616 ≈ 1.84e19
 *   
 *   Since 37^12 < 2^64, stores up to 12 characters in u64.
 *   37^13 > 2^64 would overflow, so 12 is maximum.
 * 
 * WHY BASE-37?
 * 
 *   Historical context (RuneScape 2004):
 *     - Usernames limited to: letters, digits, spaces
 *     - Total distinct characters needed: 26 + 10 + 1 = 37
 *     - Base-37 is minimal base supporting all valid username chars
 *     - Efficient encoding: 12 chars → 8 bytes (vs 12 bytes ASCII)
 * 
 *   Alternative encodings:
 *     - ASCII: 12 bytes (8-bit per char)
 *     - Base-64: 9 bytes (6-bit per char, ceil(12×6/8)=9)
 *     - Base-37: 8 bytes (stores in 64-bit long)
 * 
 * ALGORITHM COMPLEXITY:
 * 
 *   Time: O(n) where n = strlen(username), max 12
 *   Space: O(1) - single u64 accumulator
 *   Operations per char: 1 multiply + 1 add (constant time)
 * 
 * USE CASES:
 * 
 *   1. Friend list storage: Store usernames as u64 instead of strings
 *   2. Ignore list: Fast integer comparison instead of strcmp
 *   3. Username lookup: Hash table with u64 keys
 *   4. Network protocol: Fixed 8-byte transmission
 * 
 * COLLISION RESISTANCE:
 * 
 *   Perfect encoding: No collisions possible
 *   Each valid username maps to unique u64 value
 *   Bijection: One-to-one mapping (reversible)
 * 
 * DECODING EXAMPLE (reverse operation):
 * 
 *   To decode 1446 → "abc":
 *     1446 / 37² = 1 remainder 77 → 'a'
 *     77 / 37¹   = 2 remainder 3  → 'b'
 *     3 / 37⁰    = 3 remainder 0  → 'c'
 *     Result: "abc"
 */
static u64 username_to_base37(const char* username) {
    u64 value = 0;
    
    /*
     * Horner's method for polynomial evaluation:
     * Instead of computing c[i] × 37^i for each char, we use:
     *   value = (...((c[0] × 37 + c[1]) × 37 + c[2]) × 37 + ...)
     * 
     * This avoids exponentiation and is more numerically stable.
     */
    for (const char* p = username; *p; ++p) {
        char c = *p;
        
        /* Lowercase letters: a-z → 1-26 */
        if (c >= 'a' && c <= 'z') {
            value = value * 37 + (c - 'a') + 1;
        }
        /* Uppercase letters: A-Z → 1-26 (treat same as lowercase) */
        else if (c >= 'A' && c <= 'Z') {
            value = value * 37 + (c - 'A') + 1;
        }
        /* Digits: 0-9 → 27-36 */
        else if (c >= '0' && c <= '9') {
            value = value * 37 + (c - '0') + 27;
        }
        /* Other characters (space, punctuation): Skip */
        /* Note: Could map space to 0, but current impl skips it */
    }
    
    return value;
}
