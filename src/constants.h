/*******************************************************************************
 * CONSTANTS.H - Game Protocol Constants and Enumerations
 *******************************************************************************
 * 
 * EDUCATIONAL PURPOSE:
 * This file demonstrates fundamental concepts in:
 *   - Network protocol design (opcodes, response codes)
 *   - Bit flags and bitmasks (update flags, bitwise OR)
 *   - State machines (connection stages, login flow)
 *   - Coordinate systems (8-directional movement)
 *   - Magic numbers and their documentation
 * 
 * CORE CONCEPT - PROTOCOL CONSTANTS:
 * 
 * Network protocols communicate via numeric codes that both client and server
 * understand. These "magic numbers" must be:
 *   1. DOCUMENTED (why this specific value?)
 *   2. SYNCHRONIZED (client and server must match exactly)
 *   3. VERSIONED (changing them breaks compatibility)
 * 
 * EXAMPLE - LOGIN HANDSHAKE:
 * 
 *   Client                                Server
 *   ──────                                ──────
 *   [connects to port 43594]
 *         ────────────────────────────────────→
 *                                         [accept connection]
 *   
 *   [send username, password]
 *         ────────────────────────────────────→
 *                                         [verify credentials]
 *   
 *         ←────────────────────────────────────
 *                          [send LOGIN_RESPONSE_OK = 2]
 *   
 *   [enter game]
 * 
 * If server sent "2" but client expected "3", login would fail!
 * Constants like LOGIN_RESPONSE_OK ensure both sides agree.
 * 
 * HISTORICAL CONTEXT - RUNESCAPE PROTOCOL:
 * 
 * These constants are reverse-engineered from RuneScape 2004 era:
 *   - C client source code (Client3-main)
 *   - Network packet capture (Wireshark analysis)
 *   - Community documentation (RSC/OSRS wikis)
 * 
 * Values are NOT arbitrary - they match Jagex's original implementation.
 * Changing any value would make our server incompatible with real clients.
 * 
 ******************************************************************************/

#ifndef CONSTANTS_H
#define CONSTANTS_H

#include "types.h"

/*******************************************************************************
 * SERVER CAPACITY LIMITS
 *******************************************************************************
 * 
 * These constants extend the limits defined in types.h with entity-specific
 * maximums (NPCs, ground items, etc.).
 ******************************************************************************/

/*
 * MAX_NPCS - Maximum non-player characters (NPCs) in the game world
 * 
 * VALUE: 8191 NPCs
 * 
 * PROTOCOL CONSTRAINT:
 *   RuneScape uses 13-bit NPC indices in update packets:
 *     13 bits = 2^13 = 8192 possible values (0-8191)
 * 
 *   Packet bit layout:
 *     ┌──────────────┬─────────────────┬───────────┐
 *     │  NPC count   │  NPC 1 index    │ Update... │
 *     │  (variable)  │   (13 bits)     │           │
 *     └──────────────┴─────────────────┴───────────┘
 * 
 * MEMORY IMPACT:
 *   Each NPC requires ~512 bytes (position, stats, combat state, etc.)
 *   Total: 8191 * 512 bytes = 4MB (acceptable for server)
 * 
 * USAGE DISTRIBUTION:
 *   - ~2000 NPCs: Combat (guards, monsters)
 *   - ~3000 NPCs: Shops, bankers, quest NPCs
 *   - ~3000 NPCs: Ambient (chickens, cows, decoration)
 * 
 * COMPARISON TO PLAYERS:
 *   - Players: 2048 max (11-bit index)
 *   - NPCs:    8191 max (13-bit index)
 *   - More NPCs because they're shared across all players
 *     (1 shop NPC serves 100 players, but each player is unique)
 * 
 * WHY 8191 NOT 8192?:
 *   - Index 8192 would require 14 bits
 *   - 0-8191 fits exactly in 13 bits
 *   - Some protocols reserve highest value as sentinel (-1 or NULL)
 */
#define MAX_NPCS 8191

/*
 * MAX_GROUND_ITEMS - Maximum items dropped on the ground
 * 
 * VALUE: 10,000 items
 * 
 * GROUND ITEM MECHANICS:
 *   When a player kills an NPC or drops an item:
 *     1. Item spawns at (x, y, z) coordinates
 *     2. Initially visible only to player who dropped it (60 seconds)
 *     3. Then becomes visible to all players (120 seconds)
 *     4. Finally despawns if not picked up
 * 
 * MEMORY:
 *   Each ground item: ~32 bytes (item ID, quantity, position, owner, timer)
 *   Total: 10,000 * 32 = 320KB (very small)
 * 
 * WHY 10,000?:
 *   - Typical active players: 500
 *   - Items per player: ~20 (combat loot, drops)
 *   - 500 * 20 = 10,000 (perfect match!)
 * 
 * OVERFLOW BEHAVIOR:
 *   - When limit reached, despawn oldest items first (FIFO queue)
 *   - Prevents memory exhaustion from item spam
 * 
 * PERFORMANCE:
 *   - Items stored in spatial hash map (region → item list)
 *   - Lookup time: O(1) average, O(n/regions) worst case
 *   - Not performance-critical (items rarely searched globally)
 */
#define MAX_GROUND_ITEMS 10000

/*
 * MAX_PACKETS_PER_SECOND - Anti-spam rate limit
 * 
 * VALUE: 15 packets per second
 * 
 * SECURITY - DENIAL OF SERVICE PREVENTION:
 *   Malicious clients could flood server with packets:
 *     while (true) send_packet();  // 1000+ packets/sec!
 *   
 *   This would:
 *     - Consume CPU (parsing packets)
 *     - Exhaust memory (buffering data)
 *     - Degrade service for legitimate players
 * 
 * RATE LIMITING ALGORITHM:
 *   Server tracks packets per connection:
 *   
 *   struct Connection {
 *       u32 packet_count;      // Packets received this second
 *       u64 last_reset_time;   // Timestamp of last reset
 *   };
 *   
 *   On each packet:
 *     if (now - last_reset_time >= 1000ms) {
 *         packet_count = 0;
 *         last_reset_time = now;
 *     }
 *     packet_count++;
 *     if (packet_count > MAX_PACKETS_PER_SECOND) {
 *         disconnect_player("Packet spam");
 *     }
 * 
 * WHY 15?:
 *   - Normal gameplay: ~5 packets/sec (movement, clicks)
 *   - Rapid clicking: ~10 packets/sec (combat, skilling)
 *   - 15 provides headroom without allowing abuse
 * 
 * GAME TICK RELATIONSHIP:
 *   - Tick rate: 600ms = 1.67 ticks/second
 *   - 15 packets/sec ÷ 1.67 ticks/sec = ~9 packets/tick (reasonable)
 * 
 * ALTERNATIVES (not used here):
 *   - Token bucket: Allow bursts, smooth rate over time
 *   - Leaky bucket: Fixed rate, drop excess
 *   - Sliding window: More accurate, higher complexity
 */
#define MAX_PACKETS_PER_SECOND 15

/*
 * TICK_RATE - Game loop tick duration in milliseconds
 * 
 * VALUE: 600 milliseconds (0.6 seconds)
 * 
 * DUPLICATE OF types.h::TICK_RATE_MS:
 *   This is intentionally duplicated for historical reasons:
 *     - types.h: Used by low-level buffer/network code
 *     - constants.h: Used by high-level game logic
 *   
 *   Could be refactored to single definition, but kept separate
 *   to match original codebase structure.
 * 
 * See types.h for full documentation of tick rate concept.
 */
#define TICK_RATE 600

/*******************************************************************************
 * LOGIN RESPONSE CODES
 *******************************************************************************
 * 
 * PROTOCOL: Server responds to login request with single-byte status code.
 * 
 * LOGIN FLOW:
 *   1. Client sends username + password
 *   2. Server validates credentials, checks server capacity
 *   3. Server sends ONE of these response codes
 *   4. Client shows appropriate message or enters game
 * 
 * PACKET FORMAT:
 *   ┌────────────────┐
 *   │  Response Code │
 *   │    (1 byte)    │
 *   └────────────────┘
 * 
 * HISTORICAL VALUES:
 *   These exact values come from RuneScape 2004 client decompilation.
 *   Notice they're NOT sequential (2, 3, 5, 6, 7, 11, 18) - gaps exist
 *   because Jagex removed/deprecated certain response codes over time.
 ******************************************************************************/

/*
 * LOGIN_RESPONSE_OK - Successful login, enter game
 * 
 * VALUE: 2
 * 
 * MEANING: Credentials valid, server has capacity, enter game world
 * 
 * NEXT STEPS (after client receives this code):
 *   1. Client transitions to LOGGED_IN state
 *   2. Server sends player position, skills, inventory
 *   3. Client displays game interface
 * 
 * WHY 2 NOT 0?:
 *   - 0 might be confused with false/error
 *   - 1 was used in earlier protocol version (now deprecated)
 *   - 2 is arbitrary but consistent with Jagex's choice
 */
#define LOGIN_RESPONSE_OK 2

/*
 * LOGIN_RESPONSE_INVALID_CREDENTIALS - Wrong username or password
 * 
 * VALUE: 3
 * 
 * MEANING: Username doesn't exist OR password doesn't match
 * 
 * SECURITY - TIMING ATTACK PREVENTION:
 *   Server should take SAME TIME whether username exists or not:
 * 
 *   BAD (vulnerable):
 *     if (!user_exists(username))
 *         return INVALID_CREDENTIALS;  // Fast path
 *     if (!check_password(username, password))
 *         return INVALID_CREDENTIALS;  // Slow path (hash verification)
 *   
 *   Attacker can measure timing to enumerate valid usernames!
 * 
 *   GOOD (constant-time):
 *     hash = get_password_hash(username);  // Always hash, even if user missing
 *     if (!verify_password(hash, password))
 *         return INVALID_CREDENTIALS;      // Same code path
 * 
 * CLIENT DISPLAY:
 *   "Invalid username or password. Please try again."
 *   (Never reveal which one was wrong!)
 */
#define LOGIN_RESPONSE_INVALID_CREDENTIALS 3

/*
 * LOGIN_RESPONSE_ACCOUNT_ONLINE - Player already logged in
 * 
 * VALUE: 5
 * 
 * MEANING: This account is already connected from another client
 * 
 * USE CASES:
 *   - Player forgot to logout, tries to login from another computer
 *   - Connection lost but server hasn't timed out old session
 *   - Attempted account sharing (against ToS)
 * 
 * SERVER LOGIC:
 *   On login attempt:
 *     if (is_player_online(username)) {
 *         // Option 1: Reject new login (this code)
 *         return ACCOUNT_ONLINE;
 *         
 *         // Option 2: Kick old session, allow new (not used here)
 *         // disconnect_old_session(username);
 *         // return OK;
 *     }
 * 
 * SESSION TIMEOUT:
 *   If old session is dead (network failure), server should:
 *     - Detect timeout after 30 seconds of inactivity
 *     - Automatically logout stale session
 *     - Allow new login
 * 
 * CLIENT DISPLAY:
 *   "Your account is already logged in. Please try again in a few moments."
 */
#define LOGIN_RESPONSE_ACCOUNT_ONLINE 5

/*
 * LOGIN_RESPONSE_CLIENT_OUTDATED - Client version mismatch
 * 
 * VALUE: 6
 * 
 * MEANING: Client version doesn't match server version
 * 
 * VERSION CHECKING:
 *   Client sends version in login handshake:
 *     ┌─────────────┬──────────┬──────────┐
 *     │   Version   │ Username │ Password │
 *     │  (4 bytes)  │   ...    │   ...    │
 *     └─────────────┴──────────┴──────────┘
 * 
 *   Server checks:
 *     if (client_version != SERVER_VERSION) {
 *         return CLIENT_OUTDATED;
 *     }
 * 
 * WHY VERSION CHECKING?:
 *   - Protocol changes: Packet formats change between versions
 *   - Security fixes: Force clients to upgrade
 *   - Feature parity: Ensure all clients support same features
 * 
 * EXAMPLE VERSION NUMBERS:
 *   - RuneScape 2004: version 225
 *   - RuneScape 2009: version 530
 *   - Each "revision" increments version
 * 
 * CLIENT DISPLAY:
 *   "Your client is outdated. Please download the latest version."
 */
#define LOGIN_RESPONSE_CLIENT_OUTDATED 6

/*
 * LOGIN_RESPONSE_WORLD_FULL - Server at capacity
 * 
 * VALUE: 7
 * 
 * MEANING: Server has reached MAX_PLAYERS limit
 * 
 * SERVER LOGIC:
 *   if (online_players >= MAX_PLAYERS) {
 *       return WORLD_FULL;
 *   }
 * 
 * ALTERNATIVES (not implemented here):
 *   - Queue system: "You are #42 in queue"
 *   - Priority access: Members login first, free players queued
 *   - Auto-scaling: Spin up additional servers
 * 
 * REAL RUNESCAPE:
 *   - Each "world" (server) shows player count: "1523/2000"
 *   - Players can choose less crowded worlds
 *   - F2P worlds fill up faster than P2P worlds
 * 
 * CLIENT DISPLAY:
 *   "This world is currently full. Please try another world."
 */
#define LOGIN_RESPONSE_WORLD_FULL 7

/*
 * LOGIN_RESPONSE_TRY_AGAIN - Temporary server error
 * 
 * VALUE: 11
 * 
 * MEANING: Login failed due to temporary issue, retry may succeed
 * 
 * USE CASES:
 *   - Database connection lost
 *   - Authentication service timeout
 *   - Server under heavy load
 *   - Transient network error
 * 
 * SERVER LOGIC:
 *   if (!database_available()) {
 *       log_error("DB unavailable during login");
 *       return TRY_AGAIN;
 *   }
 * 
 * RETRY STRATEGY:
 *   Client should:
 *     - Wait 2-5 seconds before retry
 *     - Max 3 retry attempts
 *     - Show error if still failing
 * 
 * MONITORING:
 *   High rate of TRY_AGAIN responses indicates:
 *     - Infrastructure problems
 *     - DDoS attack
 *     - Need to scale resources
 * 
 * CLIENT DISPLAY:
 *   "Login server unavailable. Please try again in a few moments."
 */
#define LOGIN_RESPONSE_TRY_AGAIN 11

/*
 * LOGIN_RESPONSE_ACCOUNT_LOCKED - Account banned or locked
 * 
 * VALUE: 18
 * 
 * MEANING: Account disabled by administrator (ban, suspension, security)
 * 
 * USE CASES:
 *   - Permanent ban (botting, cheating, RWT)
 *   - Temporary ban (offensive language, bug abuse)
 *   - Security lock (suspicious activity detected)
 *   - Payment issue (membership charge failed)
 * 
 * DATABASE SCHEMA:
 *   table players {
 *       username VARCHAR(12)
 *       banned BOOLEAN
 *       ban_reason TEXT
 *       ban_expiry TIMESTAMP  -- NULL = permanent
 *   }
 * 
 * SERVER LOGIC:
 *   if (player.banned && (player.ban_expiry == NULL || now < player.ban_expiry)) {
 *       log_info("Banned player attempted login: %s", username);
 *       return ACCOUNT_LOCKED;
 *   }
 * 
 * REAL RUNESCAPE:
 *   - Client shows detailed ban info (reason, duration, appeal URL)
 *   - We only send code 18, client would need extra data for details
 * 
 * CLIENT DISPLAY:
 *   "Your account has been locked. Please visit the website for details."
 */
#define LOGIN_RESPONSE_ACCOUNT_LOCKED 18

/*******************************************************************************
 * PLAYER UPDATE FLAGS (BIT FLAGS)
 *******************************************************************************
 * 
 * CONCEPT - BIT FLAGS:
 * 
 * Instead of sending every field every tick, RuneScape uses "update flags"
 * to indicate which fields changed:
 * 
 *   ┌─────────────────────────────────────┐
 *   │ Update Flags (16-bit bitmask)       │  Each bit = one field
 *   ├─────────────────────────────────────┤
 *   │ 0 0 0 0 0 0 0 1 0 1 0 0 0 0 1 0     │
 *   │                 │ │         │       │
 *   │                 │ │         └─ Bit 1: APPEARANCE changed
 *   │                 │ └─────────── Bit 3: ANIMATION changed
 *   │                 └───────────── Bit 9: FORCED_MOVEMENT changed
 *   └─────────────────────────────────────┘
 * 
 * If bit N is set, corresponding field follows in packet.
 * If bit N is clear, field unchanged (saves bandwidth).
 * 
 * BITWISE OPERATIONS:
 * 
 *   // Set multiple flags
 *   u16 flags = UPDATE_APPEARANCE | UPDATE_ANIMATION;
 *   // flags = 0x1 | 0x8 = 0x9 (binary: 1001)
 * 
 *   // Check if flag set
 *   if (flags & UPDATE_APPEARANCE) {
 *       // Appearance changed, send appearance data
 *   }
 * 
 *   // Add flag
 *   flags |= UPDATE_CHAT;  // Set bit without clearing others
 * 
 *   // Remove flag
 *   flags &= ~UPDATE_CHAT;  // Clear bit without affecting others
 * 
 * PACKET ENCODING:
 * 
 *   ┌────────────┬────────────────┬─────────────┬──────────────┐
 *   │ Player ID  │  Update Flags  │ Appearance  │  Animation   │
 *   │  (11 bits) │  (variable)    │  (if bit 0) │  (if bit 3)  │
 *   └────────────┴────────────────┴─────────────┴──────────────┘
 * 
 * MEMORY SAVINGS:
 *   - Full update: ~100 bytes (all fields)
 *   - Typical tick: ~10 bytes (only position changed)
 *   - 90% bandwidth reduction!
 * 
 * FLAG VALUES:
 *   Powers of 2 (each bit represents one flag):
 *     0x1   = 2^0  = bit 0
 *     0x2   = 2^1  = bit 1
 *     0x4   = 2^2  = bit 2
 *     0x8   = 2^3  = bit 3
 *     0x10  = 2^4  = bit 4
 *     ... and so on
 ******************************************************************************/

/*
 * UPDATE_APPEARANCE - Player appearance changed
 * 
 * VALUE: 0x1 (bit 0)
 * 
 * TRIGGERED WHEN:
 *   - Player equips/unequips armor or weapon
 *   - Player changes clothes color
 *   - Player changes hairstyle
 * 
 * DATA SENT (if flag set):
 *   ┌───────────┬────────────┬────────────┬─────────────┬─────┐
 *   │  Gender   │  Head Slot │ Chest Slot │  Leg Slot   │ ... │
 *   │ (1 byte)  │  (2 bytes) │ (2 bytes)  │  (2 bytes)  │     │
 *   └───────────┴────────────┴────────────┴─────────────┴─────┘
 *   Total: ~20 bytes
 * 
 * FREQUENCY:
 *   - Rare (only when changing equipment)
 *   - ~0.1% of updates have this flag
 */
#define UPDATE_APPEARANCE 0x1

/*
 * UPDATE_CHAT - Player sent chat message
 * 
 * VALUE: 0x2 (bit 1)
 * 
 * TRIGGERED WHEN:
 *   - Player types message and presses enter
 * 
 * DATA SENT (if flag set):
 *   ┌───────────┬────────────┬─────────────────────────┐
 *   │   Color   │   Effect   │   Compressed Text       │
 *   │ (1 byte)  │ (1 byte)   │ (variable, max ~80)     │
 *   └───────────┴────────────┴─────────────────────────┘
 * 
 * TEXT COMPRESSION:
 *   RuneScape uses Huffman-like encoding to compress text:
 *     - Common words: "the" → 3 bits
 *     - Uncommon letters: "q" → 7 bits
 *     - Reduces "hello world" from 11 bytes to ~6 bytes
 * 
 * FREQUENCY:
 *   - Medium (players chat occasionally)
 *   - ~5% of updates have this flag
 */
#define UPDATE_CHAT 0x2

/*
 * UPDATE_GRAPHICS - Graphical effect on player
 * 
 * VALUE: 0x4 (bit 2)
 * 
 * TRIGGERED WHEN:
 *   - Player casts spell (magic sparkles)
 *   - Player teleports (poof animation)
 *   - Player levels up (fireworks)
 * 
 * DATA SENT (if flag set):
 *   ┌─────────────┬────────────┬─────────────┐
 *   │ Graphic ID  │   Height   │    Delay    │
 *   │  (2 bytes)  │ (1 byte)   │  (2 bytes)  │
 *   └─────────────┴────────────┴─────────────┘
 *   Total: 5 bytes
 * 
 * EXAMPLES:
 *   - Graphic 86:  Teleport animation
 *   - Graphic 199: Ice Barrage spell
 *   - Graphic 308: Level up fireworks
 * 
 * FREQUENCY:
 *   - Low (only during combat/skilling)
 *   - ~2% of updates have this flag
 */
#define UPDATE_GRAPHICS 0x4

/*
 * UPDATE_ANIMATION - Player animation changed
 * 
 * VALUE: 0x8 (bit 3)
 * 
 * TRIGGERED WHEN:
 *   - Player starts/stops walking/running
 *   - Player attacks (weapon swing)
 *   - Player performs emote (/dance, /wave)
 * 
 * DATA SENT (if flag set):
 *   ┌──────────────┬─────────────┐
 *   │ Animation ID │    Delay    │
 *   │  (2 bytes)   │  (1 byte)   │
 *   └──────────────┴─────────────┘
 *   Total: 3 bytes
 * 
 * EXAMPLES:
 *   - Animation 808: Walk
 *   - Animation 824: Run
 *   - Animation 422: Punch (unarmed combat)
 *   - Animation 1167: Dragon scimitar slash
 * 
 * CLIENT BEHAVIOR:
 *   - Client plays animation on player model
 *   - Animation loops until another UPDATE_ANIMATION received
 * 
 * FREQUENCY:
 *   - Very high (movement every tick)
 *   - ~80% of updates have this flag
 */
#define UPDATE_ANIMATION 0x8

/*
 * UPDATE_FORCED_CHAT - NPC-like overhead text
 * 
 * VALUE: 0x10 (bit 4)
 * 
 * TRIGGERED WHEN:
 *   - Scripted event forces player to "say" something
 *   - Example: Tutorial NPC makes player say "Hello!"
 * 
 * DIFFERENCE FROM UPDATE_CHAT:
 *   - UPDATE_CHAT: Player types voluntarily (chat bubble)
 *   - UPDATE_FORCED_CHAT: Game script forces text (overhead label)
 * 
 * DATA SENT (if flag set):
 *   ┌─────────────────────────┐
 *   │      Text String        │
 *   │  (variable, max ~255)   │
 *   └─────────────────────────┘
 * 
 * USE CASES:
 *   - Quest dialogue where player character speaks
 *   - Tutorial instructions
 *   - Minigames (e.g., "I have the flag!")
 * 
 * FREQUENCY:
 *   - Extremely rare (only in scripted content)
 *   - <0.01% of updates have this flag
 */
#define UPDATE_FORCED_CHAT 0x10

/*
 * UPDATE_FACE_ENTITY - Player turned to face another entity
 * 
 * VALUE: 0x20 (bit 5)
 * 
 * TRIGGERED WHEN:
 *   - Player attacks NPC/player (auto-face target)
 *   - Player clicks to interact (talk to NPC, use object)
 * 
 * DATA SENT (if flag set):
 *   ┌──────────────────┐
 *   │   Entity Index   │  NPC = 0-8191, Player = 32768 + (0-2047)
 *   │    (2 bytes)     │
 *   └──────────────────┘
 * 
 * ENTITY ENCODING:
 *   - NPC:    0-8191 (direct index)
 *   - Player: 32768 + player_index (bit 15 set = player)
 *   - None:   65535 (0xFFFF = stop facing)
 * 
 * CLIENT BEHAVIOR:
 *   - Smoothly rotates player model to face target
 *   - Overrides normal rotation (from movement)
 *   - Resets when combat ends
 * 
 * FREQUENCY:
 *   - Medium (during combat/interaction)
 *   - ~10% of updates have this flag
 */
#define UPDATE_FACE_ENTITY 0x20

/*
 * UPDATE_FACE_POSITION - Player turned to face coordinates
 * 
 * VALUE: 0x40 (bit 6)
 * 
 * TRIGGERED WHEN:
 *   - Player clicks ground tile (path-finding)
 *   - Scripted event rotates player
 * 
 * DATA SENT (if flag set):
 *   ┌─────────────┬─────────────┐
 *   │  X Coord    │  Y Coord    │  (absolute world coordinates)
 *   │ (2 bytes)   │ (2 bytes)   │
 *   └─────────────┴─────────────┘
 *   Total: 4 bytes
 * 
 * COORDINATE ENCODING:
 *   - X and Y are DOUBLED for sub-tile precision:
 *     x = 3200, y = 3400 → Face center of tile (1600, 1700)
 *     x = 3201, y = 3400 → Face slightly east of center
 * 
 * DIFFERENCE FROM UPDATE_FACE_ENTITY:
 *   - FACE_ENTITY: Track moving target (player/NPC)
 *   - FACE_POSITION: Look at fixed location (tile, object)
 * 
 * FREQUENCY:
 *   - Low (mostly for scripted scenes)
 *   - ~1% of updates have this flag
 */
#define UPDATE_FACE_POSITION 0x40

/*
 * UPDATE_HIT - Player took damage (first hit)
 * 
 * VALUE: 0x80 (bit 7)
 * 
 * TRIGGERED WHEN:
 *   - Combat damage dealt to player
 *   - Environmental damage (poison, fire)
 * 
 * DATA SENT (if flag set):
 *   ┌─────────────┬─────────────┬─────────────┐
 *   │   Damage    │  Hit Type   │   Hitpoints │
 *   │  (1 byte)   │  (1 byte)   │  (1 byte)   │
 *   └─────────────┴─────────────┴─────────────┘
 *   Total: 3 bytes
 * 
 * HIT TYPES:
 *   0 = Miss (blue "0" splat)
 *   1 = Normal hit (red damage splat)
 *   2 = Poison (green splat)
 *   3 = Disease (orange splat)
 * 
 * CLIENT DISPLAY:
 *   - Shows damage number above player
 *   - Updates health bar
 *   - Plays hit animation/sound
 * 
 * FREQUENCY:
 *   - Medium (during combat)
 *   - ~15% of updates have this flag
 */
#define UPDATE_HIT 0x80

/*
 * UPDATE_HIT2 - Player took damage (second hit, multi-combat)
 * 
 * VALUE: 0x100 (bit 8)
 * 
 * TRIGGERED WHEN:
 *   - Player takes multiple hits in same tick
 *   - Example: Fighting 2 enemies simultaneously
 * 
 * DATA SENT (if flag set):
 *   Same format as UPDATE_HIT (3 bytes)
 * 
 * MULTI-HIT MECHANICS:
 *   - Most combat: 1 hit per tick (UPDATE_HIT)
 *   - Multi-combat zones: 2+ hits per tick (UPDATE_HIT + UPDATE_HIT2)
 *   - Client displays both splats stacked vertically
 * 
 * WHY SEPARATE FLAG?:
 *   - Could have variable-length hit array: [hit1, hit2, hit3...]
 *   - RuneScape limits to 2 hits/tick for balance/simplicity
 *   - Separate flag is simpler than variable-length encoding
 * 
 * FREQUENCY:
 *   - Low (only in multi-combat areas)
 *   - ~2% of updates have this flag
 */
#define UPDATE_HIT2 0x100

/*
 * UPDATE_FORCED_MOVEMENT - Scripted player movement
 * 
 * VALUE: 0x200 (bit 9)
 * 
 * TRIGGERED WHEN:
 *   - Agility shortcut (swing across rope)
 *   - Magic carpet ride
 *   - Knockback from attack
 *   - Cutscene movement
 * 
 * DATA SENT (if flag set):
 *   ┌──────────┬──────────┬──────────┬──────────┬──────────┬──────────┐
 *   │ Start X  │ Start Y  │  End X   │  End Y   │  Speed   │Direction │
 *   │ (1 byte) │ (1 byte) │ (1 byte) │ (1 byte) │ (2 bytes)│ (1 byte) │
 *   └──────────┴──────────┴──────────┴──────────┴──────────┴──────────┘
 *   Total: 8 bytes
 * 
 * COORDINATE ENCODING:
 *   - Relative to player's current region
 *   - start_x = current_x - region_base_x
 * 
 * SPEED:
 *   - Duration in game ticks (600ms each)
 *   - Speed 5 = move over 3 seconds (5 * 600ms)
 * 
 * CLIENT BEHAVIOR:
 *   - Smoothly interpolates player from start to end
 *   - Overrides normal movement controls
 *   - Player cannot move until animation finishes
 * 
 * FREQUENCY:
 *   - Very rare (only during special actions)
 *   - <0.1% of updates have this flag
 */
#define UPDATE_FORCED_MOVEMENT 0x200

/*******************************************************************************
 * NETWORK STAGE ENUMERATION
 *******************************************************************************
 * 
 * State machine for player connection lifecycle.
 ******************************************************************************/

/*
 * NetworkStage - Connection state machine
 * 
 * STATE TRANSITIONS:
 * 
 *   [Client connects]
 *         ↓
 *   NETWORK_CONNECTED  ──────┐
 *         ↓                  │
 *   [Login handshake]        │ [Connection error]
 *         ↓                  │
 *   NETWORK_LOGIN ───────────┤
 *         ↓                  │
 *   [Credentials verified]   │
 *         ↓                  │
 *   NETWORK_LOGGED_IN ───────┤
 *         ↓                  │
 *   [Logout/disconnect]      │
 *         ↓                  │
 *   NETWORK_LOGGED_OUT ←─────┘
 * 
 * STATE BEHAVIORS:
 * 
 *   NETWORK_CONNECTED:
 *     - TCP connection established
 *     - Waiting for login handshake
 *     - Timeout: 10 seconds (prevent idle connections)
 * 
 *   NETWORK_LOGIN:
 *     - Receiving username, password, version
 *     - Validating credentials against database
 *     - Will send LOGIN_RESPONSE_* code
 * 
 *   NETWORK_LOGGED_IN:
 *     - Active gameplay
 *     - Processing game packets (movement, combat, etc.)
 *     - Sending update packets every tick
 * 
 *   NETWORK_LOGGED_OUT:
 *     - Cleanup state before disconnection
 *     - Save player data to database
 *     - Remove from online player list
 *     - Close socket
 * 
 * ENUM VALUES:
 *   Auto-assigned by compiler (0, 1, 2, 3)
 *   Order matters for state machine logic
 * 
 * USAGE:
 *   struct Connection {
 *       NetworkStage stage;
 *   };
 *   
 *   switch (conn->stage) {
 *       case NETWORK_CONNECTED:
 *           handle_handshake(conn);
 *           break;
 *       case NETWORK_LOGIN:
 *           handle_login(conn);
 *           break;
 *       // ...
 *   }
 */
typedef enum {
    NETWORK_CONNECTED,    /* TCP connected, waiting for handshake */
    NETWORK_LOGIN,        /* Receiving login credentials */
    NETWORK_LOGGED_IN,    /* Active gameplay */
    NETWORK_LOGGED_OUT    /* Cleanup before disconnect */
} NetworkStage;

/*******************************************************************************
 * MOVEMENT DIRECTION ENUMERATION
 *******************************************************************************
 * 
 * 8-directional movement system (cardinal + diagonal).
 ******************************************************************************/

/*
 * Direction - 8-way movement + stationary
 * 
 * COORDINATE SYSTEM:
 * 
 *        North (Y+)
 *           ↑
 *           │
 *   West ←──┼──→ East
 *   (X-)    │    (X+)
 *           ↓
 *        South (Y-)
 * 
 * DIRECTION LAYOUT:
 * 
 *     NW(0)   N(1)   NE(2)
 *        ↖     ↑     ↗
 *         \    |    /
 *          \   |   /
 *   W(3) ←──   ·   ──→ E(4)
 *          /   |   \
 *         /    |    \
 *        ↙     ↓     ↘
 *     SW(5)   S(6)   SE(7)
 * 
 * DELTA TABLE (how each direction changes X/Y):
 * 
 *   DIR          │ VALUE │ ΔX │ ΔY │
 *   ─────────────┼───────┼────┼────┤
 *   NORTH_WEST   │   0   │ -1 │ +1 │
 *   NORTH        │   1   │  0 │ +1 │
 *   NORTH_EAST   │   2   │ +1 │ +1 │
 *   WEST         │   3   │ -1 │  0 │
 *   EAST         │   4   │ +1 │  0 │
 *   SOUTH_WEST   │   5   │ -1 │ -1 │
 *   SOUTH        │   6   │  0 │ -1 │
 *   SOUTH_EAST   │   7   │ +1 │ -1 │
 *   NONE         │  -1   │  0 │  0 │ (stationary)
 * 
 * USAGE IN CODE:
 * 
 *   const i8 delta_x[8] = {-1, 0, +1, -1, +1, -1, 0, +1};
 *   const i8 delta_y[8] = {+1, +1, +1, 0, 0, -1, -1, -1};
 *   
 *   void move_player(Player* p, Direction dir) {
 *       if (dir == DIR_NONE) return;
 *       p->x += delta_x[dir];
 *       p->y += delta_y[dir];
 *   }
 * 
 * PACKET ENCODING:
 *   Direction fits in 3 bits (0-7), saves bandwidth:
 *     buffer_write_bits(buf, 3, DIR_NORTH_EAST);
 * 
 * WHY -1 FOR NONE?:
 *   - 0-7 are valid array indices
 *   - -1 is sentinel value (clearly invalid index)
 *   - Can check: if (dir >= 0) then it's a valid direction
 * 
 * DIAGONAL MOVEMENT:
 *   - Diagonal moves same distance as cardinal (unrealistic but simple)
 *   - Real RuneScape: sqrt(2) is approximately 1.41x distance (not implemented here)
 */
typedef enum {
    DIR_NORTH_WEST = 0,   /* (-1, +1) - up and left */
    DIR_NORTH      = 1,   /* ( 0, +1) - straight up */
    DIR_NORTH_EAST = 2,   /* (+1, +1) - up and right */
    DIR_WEST       = 3,   /* (-1,  0) - straight left */
    DIR_EAST       = 4,   /* (+1,  0) - straight right */
    DIR_SOUTH_WEST = 5,   /* (-1, -1) - down and left */
    DIR_SOUTH      = 6,   /* ( 0, -1) - straight down */
    DIR_SOUTH_EAST = 7,   /* (+1, -1) - down and right */
    DIR_NONE       = -1   /* ( 0,  0) - not moving */
} Direction;

#endif /* CONSTANTS_H */
