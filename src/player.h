/*******************************************************************************
 * PLAYER.H - Player Entity State and Management
 *******************************************************************************
 * 
 * EDUCATIONAL PURPOSE:
 * This file demonstrates fundamental concepts in:
 *   - Entity-Component architecture (Player as aggregate of components)
 *   - State machine design (connection lifecycle)
 *   - Network protocol integration (cipher state, packet buffers)
 *   - Game world synchronization (position, region tracking)
 *   - Resource management (socket lifecycle, memory cleanup)
 *   - Update flags (dirty tracking for network synchronization)
 * 
 * CORE CONCEPT - PLAYER ENTITY:
 * 
 * In multiplayer games, a "Player" entity represents a connected user's state.
 * This includes:
 *   1. Network state (socket, buffers, encryption)
 *   2. Spatial state (position, movement queue)
 *   3. Identity (username, index in player array)
 *   4. Synchronization state (update flags, region tracking)
 * 
 * PLAYER LIFECYCLE:
 * ┌────────────────────────────────────────────────────────────────────┐
 * │  State: DISCONNECTED                                               │
 * │    ↓ (client connects to server)                                   │
 * │  player_set_socket()                                               │
 * │    ↓                                                               │
 * │  State: CONNECTED                                                  │
 * │    ↓ (client sends login packet)                                   │
 * │  login_process() [in login.c]                                      │
 * │    ↓                                                               │
 * │  State: LOGGING_IN                                                 │
 * │    ↓ (credentials verified, world loaded)                          │
 * │  State: LOGGED_IN                                                  │
 * │    ↓ (client sends logout or disconnect)                           │
 * │  player_disconnect()                                               │
 * │    ↓                                                               │
 * │  State: DISCONNECTED                                               │
 * └────────────────────────────────────────────────────────────────────┘
 * 
 * STATE MACHINE TRANSITIONS:
 *   DISCONNECTED → CONNECTED:      player_set_socket(player, fd)
 *   CONNECTED → LOGGING_IN:        Set by login handler
 *   LOGGING_IN → LOGGED_IN:        Set by login handler after success
 *   (any state) → DISCONNECTED:    player_disconnect(player)
 * 
 * PLAYER SYNCHRONIZATION:
 * 
 * RuneScape uses a "viewport" system where each player sees entities nearby:
 *   - Player A can see player B if distance < 15 tiles
 *   - Server sends "player update" packets every tick (600ms)
 *   - Update includes: position, appearance, animations, chat
 * 
 * VIEWPORT VISUALIZATION:
 *   Player A at (3200, 3200):
 *   
 *   ┌─────────────────────────────────────────┐
 *   │                                         │
 *   │        ┌───────────────────┐            │  Viewport range:
 *   │        │    15x15 tiles    │            │    ±15 tiles from
 *   │        │                   │            │    player position
 *   │        │        (A)        │            │
 *   │        │                   │            │
 *   │        └───────────────────┘            │
 *   │                                         │
 *   │  Players within viewport:               │
 *   │    - B at (3205, 3202) ← visible        │
 *   │    - C at (3250, 3250) ← NOT visible    │
 *   └─────────────────────────────────────────┘
 * 
 * UPDATE FLAGS:
 * 
 * To optimize network traffic, the server only sends changed data:
 *   - UPDATE_FLAG_APPEARANCE:  Changed clothing/colors
 *   - UPDATE_FLAG_ANIMATION:   Playing an animation
 *   - UPDATE_FLAG_CHAT:        Sent a chat message
 *   - UPDATE_FLAG_MOVEMENT:    Walked/ran this tick
 * 
 * Each tick:
 *   1. Process player actions → set update_flags bits
 *   2. Send update packet with only flagged data
 *   3. Clear update_flags for next tick
 * 
 * REGION SYSTEM:
 * 
 * The game world is divided into 64×64 tile regions for efficient culling:
 *   - Player's region: position.x >> 6, position.z >> 6
 *   - When region changes, send new map data
 *   - region_changed flag triggers map update packet
 * 
 * REGION CHANGE EXAMPLE:
 *   Player at (3199, 3200):  Region (49, 50)
 *     Walks east to (3200, 3200):  Region (50, 50)  ← Changed!
 *     → region_changed = true
 *     → Send map data for region (50, 50)
 * 
 * MEMORY LAYOUT:
 * ┌──────────────────────────────────────────────────────────────────────┐
 * │ Player struct (~8KB typical)                                         │
 * ├──────────────────────────────────────────────────────────────────────┤
 * │ index           u32 (4 bytes)      Player slot in server array       │
 * │ socket_fd       i32 (4 bytes)      TCP socket file descriptor        │
 * │ state           u32 (4 bytes)      PlayerState enum                  │
 * ├──────────────────────────────────────────────────────────────────────┤
 * │ username        char[13]           Null-terminated string            │
 * │ password        char[64]           Hashed password (not plaintext!)  │
 * ├──────────────────────────────────────────────────────────────────────┤
 * │ position        Position (16B)     World coordinates + height        │
 * │ movement        MovementHandler    Waypoint queue (~100-800 bytes)   │
 * ├──────────────────────────────────────────────────────────────────────┤
 * │ in_cipher       ISAACCipher (4KB)  Decrypt incoming packets          │
 * │ out_cipher      ISAACCipher (4KB)  Encrypt outgoing packets          │
 * ├──────────────────────────────────────────────────────────────────────┤
 * │ in_buffer       u8[5000]           Incoming packet accumulator       │
 * │ out_buffer      u8[5000]           Outgoing packet builder           │
 * ├──────────────────────────────────────────────────────────────────────┤
 * │ update_flags    u32 (4 bytes)      Dirty bits for sync               │
 * │ login_time      u64 (8 bytes)      Timestamp of login (milliseconds) │
 * └──────────────────────────────────────────────────────────────────────┘
 * 
 * TOTAL SIZE: ~18KB per player (varies with movement queue size)
 * 
 * SERVER CAPACITY:
 *   With MAX_PLAYERS = 2048:
 *     2048 × 18KB = 36MB for player array
 *     (Acceptable for modern servers with GB of RAM)
 * 
 ******************************************************************************/

#ifndef PLAYER_H
#define PLAYER_H

#include "types.h"
#include "position.h"
#include "movement.h"
#include "isaac.h"

/*******************************************************************************
 * PLAYERSTATE - Connection Lifecycle State Machine
 *******************************************************************************
 * 
 * STATES:
 *   DISCONNECTED:  No active connection
 *                  - socket_fd = -1
 *                  - Player slot is available for reuse
 *                  - All data reset to defaults
 * 
 *   CONNECTED:     TCP connection established, awaiting login
 *                  - socket_fd >= 0 (valid file descriptor)
 *                  - Handshake complete, ready to receive login packet
 *                  - Timeout: 10 seconds (if no login, disconnect)
 * 
 *   LOGGING_IN:    Credentials received, processing login
 *                  - Username/password set
 *                  - Verifying against database (or file)
 *                  - Loading player save data
 *                  - Initializing ciphers with session keys
 * 
 *   LOGGED_IN:     Fully authenticated, in game world
 *                  - Can send/receive game packets
 *                  - Visible to other players
 *                  - Position updates broadcast every tick
 * 
 * STATE TRANSITIONS:
 * ┌───────────────────────────────────────────────────────────────────┐
 * │                                                                   │
 * │  [DISCONNECTED] ──accept()──> [CONNECTED]                         │
 * │       ↑                            │                              │
 * │       │                     receive login packet                  │
 * │       │                            ↓                              │
 * │  disconnect()            [LOGGING_IN]                             │
 * │       │                            │                              │
 * │       │                     verify credentials                    │
 * │       │                            ↓                              │
 * │       └───────────────────── [LOGGED_IN]                          │
 * │                                                                   │
 * └───────────────────────────────────────────────────────────────────┘
 * 
 * TIMEOUT HANDLING:
 *   State         Timeout     Action
 *   CONNECTED     10s         Disconnect (no login attempt)
 *   LOGGING_IN    5s          Disconnect (login taking too long)
 *   LOGGED_IN     60s         Disconnect (no activity/keepalive)
 * 
 * INVARIANTS:
 *   - state == DISCONNECTED  →  socket_fd == -1
 *   - state != DISCONNECTED  →  socket_fd >= 0
 *   - state == LOGGED_IN     →  username[0] != '\0'
 * 
 ******************************************************************************/
typedef enum {
    PLAYER_STATE_DISCONNECTED,   /* No connection, slot available */
    PLAYER_STATE_CONNECTED,      /* TCP connected, awaiting login */
    PLAYER_STATE_LOGGING_IN,     /* Processing credentials */
    PLAYER_STATE_LOGGED_IN       /* In game world */
} PlayerState;

/*******************************************************************************
 * PLAYER - Complete Player Entity State
 *******************************************************************************
 * 
 * FIELDS:
 * 
 * === IDENTITY & NETWORK ===
 *   index:          Player slot index in server's player array [0, MAX_PLAYERS)
 *                   Used for network protocol (player indices in update packets)
 * 
 *   socket_fd:      TCP socket file descriptor
 *                   -1 = disconnected, >= 0 = connected
 *                   Used for send() and recv() syscalls
 * 
 *   state:          Current connection state (see PlayerState enum)
 * 
 * === CREDENTIALS ===
 *   username:       Player's login name (max 12 chars + null terminator)
 *                   Stored as lowercase for case-insensitive comparison
 *                   Example: "zezima\0"
 * 
 *   password:       Hashed password (NOT plaintext!)
 *                   Typically SHA256 or similar
 *                   64 bytes to accommodate various hash algorithms
 * 
 * === SPATIAL STATE ===
 *   position:       Current world coordinates (x, z, height)
 *                   Updated by player_process_movement() each tick
 * 
 *   movement:       Waypoint queue for walking/running
 *                   Filled by client movement packets
 *                   Processed by player_process_movement()
 * 
 * === ENCRYPTION ===
 *   in_cipher:      ISAAC cipher for decrypting incoming packets
 *                   Initialized during login with client seed
 * 
 *   out_cipher:     ISAAC cipher for encrypting outgoing packets
 *                   Initialized during login with server seed
 * 
 * === SYNCHRONIZATION ===
 *   needs_placement: Player needs full position update
 *                    Set to true on login or teleport
 *                    Sends absolute coordinates (not delta)
 * 
 *   teleporting:     Player is teleporting (special animation)
 *                    Cancels movement queue
 *                    Sends teleport graphics to nearby players
 * 
 *   region_changed:  Player crossed region boundary
 *                    Set to true when position.x >> 6 changes
 *                    Triggers map data packet (new chunks)
 * 
 *   placement_ticks: Counter for placement update delay
 *                    Real RS waits a few ticks before full placement
 *                    Prevents visual glitches
 * 
 * === MOVEMENT STATE (for this tick) ===
 *   primary_direction:   Walk direction [0-7] or -1 (not moving)
 *                        Encoded in player update packet
 * 
 *   secondary_direction: Run direction [0-7] or -1 (not running)
 *                        Only set when running (2 tiles per tick)
 * 
 * === PACKET BUFFERS ===
 *   in_buffer:      Accumulator for partial packets from recv()
 *                   TCP is stream-based, packets may arrive fragmented
 *                   Example: Packet size = 100 bytes
 *                            recv() returns 60 bytes → store in buffer
 *                            next recv() returns 40 bytes → complete packet
 * 
 *   in_buffer_size: Number of bytes currently in in_buffer
 * 
 *   out_buffer:     Builder for outgoing packets
 *                   Multiple small packets batched into one send()
 *                   Example: 10 NPC update packets → one send() call
 * 
 *   out_buffer_size: Number of bytes currently in out_buffer
 * 
 * === UPDATE FLAGS ===
 *   update_flags:   Bitmask of changes this tick (see constants.h)
 *                   Bit 0: Appearance changed
 *                   Bit 1: Animation playing
 *                   Bit 2: Chat message sent
 *                   Bit 3: Forced movement (knockback)
 *                   Cleared after player update packet sent
 * 
 *   login_time:     Unix timestamp (milliseconds) of login
 *                   Used for session duration tracking
 *                   Example: 1700000000000 (2023-11-15)
 * 
 * MEMORY LAYOUT EXAMPLE (actual addresses vary):
 * ┌─────────────────────────────────────────────────┐
 * │ Address   Field              Value              │
 * ├─────────────────────────────────────────────────┤
 * │ 0x1000    index              42                 │
 * │ 0x1004    socket_fd          5                  │
 * │ 0x1008    state              LOGGED_IN          │
 * │ 0x100C    username           "player1\0"        │
 * │ 0x1019    password           [hash bytes...]    │
 * │ 0x1059    position.x         3222               │
 * │ 0x105D    position.z         3218               │
 * │ 0x1061    position.height    0                  │
 * │ 0x1065    movement           [waypoint queue]   │
 * │ 0x2000    in_cipher          [ISAAC state]      │
 * │ 0x3000    out_cipher         [ISAAC state]      │
 * │ 0x4000    in_buffer          [packet bytes]     │
 * │ 0x5388    out_buffer         [packet bytes]     │
 * │ 0x6710    update_flags       0x00000008         │
 * │ 0x6714    login_time         1700000000000      │
 * └─────────────────────────────────────────────────┘
 * 
 * TYPICAL VALUES AT RUNTIME:
 *   Active player:
 *     state = LOGGED_IN
 *     socket_fd = 5 (example)
 *     username = "zezima"
 *     position = {3222, 3218, 0}  (Lumbridge)
 *     in_buffer_size = 0 (just processed)
 *     out_buffer_size = 120 (pending send)
 *     update_flags = 0x0001 (appearance updated)
 * 
 *   Disconnected slot:
 *     state = DISCONNECTED
 *     socket_fd = -1
 *     username = "" (empty)
 *     in_buffer_size = 0
 *     out_buffer_size = 0
 *     update_flags = 0
 * 
 ******************************************************************************/
typedef struct {
    u32 index;                              /* Player array index [0, MAX_PLAYERS) */
    i32 socket_fd;                          /* TCP socket (-1 if disconnected) */
    PlayerState state;                      /* Connection lifecycle state */
    
    char username[MAX_USERNAME_LENGTH + 1]; /* Login name (null-terminated) */
    char password[64];                      /* Hashed password */
    
    Position position;                      /* World coordinates */
    MovementHandler movement;               /* Waypoint queue */
    
    u32 origin_x;                           /* Last LOAD_AREA origin X coordinate */
    u32 origin_z;                           /* Last LOAD_AREA origin Z coordinate */
    
    ISAACCipher in_cipher;                  /* Decrypt incoming packets */
    ISAACCipher out_cipher;                 /* Encrypt outgoing packets */
    
    bool needs_placement;                   /* Requires full position update */
    bool teleporting;                       /* Teleport in progress */
    bool region_changed;                    /* Crossed region boundary */
    u8 placement_ticks;                     /* Placement delay counter */
    
    i32 primary_direction;                  /* Walk direction this tick [-1, 7] */
    i32 secondary_direction;                /* Run direction this tick [-1, 7] */
    
    u8 in_buffer[MAX_PACKET_SIZE];          /* Incoming packet accumulator */
    u32 in_buffer_size;                     /* Bytes in in_buffer */
    
    u8 out_buffer[MAX_PACKET_SIZE];         /* Outgoing packet builder */
    u32 out_buffer_size;                    /* Bytes in out_buffer */
    
    u32 update_flags;                       /* Dirty bits for synchronization */
    u64 login_time;                         /* Login timestamp (milliseconds) */
    
    /* === PERSISTENT DATA (saved to disk) === */
    i8 body[7];                             /* Appearance: body parts (-1 = hidden) */
    u8 colors[5];                           /* Appearance: color indices */
    u8 gender;                              /* Gender: 0=male, 1=female */
    bool design_complete;                   /* Player has set appearance (tutorial flag) */
    bool allow_design;                      /* Allow player design packet (session only) */
    
    u32 experience[21];                     /* Skill XP values */
    u8 levels[21];                          /* Current skill levels (can be boosted) */
    
    u16 runenergy;                          /* Run energy (0-10000, 10000=100%) */
    u32 playtime;                           /* Total ticks logged in */
    u64 last_login;                         /* Last login timestamp (milliseconds) */
} Player;

/*******************************************************************************
 * PUBLIC API
 ******************************************************************************/

/*
 * player_init - Initialize player entity
 * 
 * @param player  Pointer to Player struct to initialize
 * @param index   Player slot index in server array [0, MAX_PLAYERS)
 * 
 * ALGORITHM:
 *   1. Zero entire struct: memset(player, 0, sizeof(Player))
 *   2. Set index: player->index = index
 *   3. Set socket_fd: player->socket_fd = -1 (disconnected)
 *   4. Set state: player->state = PLAYER_STATE_DISCONNECTED
 *   5. Initialize position: position_init(&player->position, 3222, 3218, 0)
 *   6. Initialize movement: movement_init(&player->movement)
 *   7. Set directions: primary_direction = secondary_direction = -1
 *   8. Set placement_ticks: 0
 * 
 * INITIAL POSITION:
 *   Default spawn: (3222, 3218, height 0)
 *   Location: Lumbridge castle courtyard
 *   Why here? Standard RuneScape tutorial island exit point
 * 
 * INITIAL STATE AFTER INIT:
 *   ┌────────────────────────────────────────┐
 *   │ index              = <provided>        │
 *   │ socket_fd          = -1                │
 *   │ state              = DISCONNECTED      │
 *   │ username           = "" (empty)        │
 *   │ position           = (3222, 3218, 0)   │
 *   │ movement           = (empty queue)     │
 *   │ primary_direction  = -1                │
 *   │ secondary_direction= -1                │
 *   │ in_buffer_size     = 0                 │
 *   │ out_buffer_size    = 0                 │
 *   │ update_flags       = 0                 │
 *   │ login_time         = 0                 │
 *   └────────────────────────────────────────┘
 * 
 * USAGE:
 *   Player players[MAX_PLAYERS];
 *   for (u32 i = 0; i < MAX_PLAYERS; i++) {
 *     player_init(&players[i], i);
 *   }
 * 
 * COMPLEXITY: O(1) time
 */
void player_init(Player* player, u32 index);

/*
 * player_destroy - Clean up player resources
 * 
 * @param player  Pointer to Player to clean up
 * 
 * ALGORITHM:
 *   1. Free movement waypoints: movement_destroy(&player->movement)
 *   2. If socket open (socket_fd >= 0):
 *        a. Close socket: close(socket_fd) or closesocket(socket_fd)
 *        b. Set socket_fd = -1
 * 
 * RESOURCE CLEANUP:
 *   - Frees all heap-allocated waypoints in movement queue
 *   - Closes TCP socket (sends FIN packet to client)
 *   - Does NOT zero struct (use player_init to reuse slot)
 * 
 * PLATFORM DIFFERENCES:
 *   Windows:  closesocket(socket_fd)  // Winsock API
 *   POSIX:    close(socket_fd)        // Berkeley Sockets
 * 
 * TCP CONNECTION TEARDOWN:
 *   close() triggers four-way handshake:
 *     Server sends FIN
 *     Client sends ACK
 *     Client sends FIN
 *     Server sends ACK
 *   Connection fully closed after ~1 second
 * 
 * WHEN TO CALL:
 *   - Player logout (graceful disconnect)
 *   - Player timeout (no activity)
 *   - Server shutdown (cleanup all players)
 *   - Before player_init() to reuse slot
 * 
 * EXAMPLE - PLAYER LOGOUT:
 *   void handle_logout(Player* player) {
 *     save_player_data(player);  // Persist to database
 *     player_destroy(player);    // Close socket, free memory
 *     player_init(player, player->index);  // Reset for reuse
 *   }
 * 
 * COMPLEXITY: O(n) time where n = movement.waypoint_count
 */
void player_destroy(Player* player);

/*
 * player_disconnect - Disconnect player and reset state
 * 
 * @param player  Pointer to Player to disconnect
 * 
 * ALGORITHM:
 *   1. Set state = PLAYER_STATE_DISCONNECTED
 *   2. Call player_destroy(player) to cleanup resources
 * 
 * DIFFERENCE FROM player_destroy:
 *   - player_destroy:    Cleanup resources, don't change state
 *   - player_disconnect: Set DISCONNECTED state, then cleanup
 * 
 * STATE TRANSITION:
 *   (any state) → DISCONNECTED
 * 
 * USAGE:
 *   Forcefully disconnect a player:
 *     player_disconnect(&player);
 *   
 *   Graceful logout:
 *     send_logout_packet(&player);
 *     player_disconnect(&player);
 * 
 * SIDE EFFECTS:
 *   - Socket closed (client receives TCP FIN)
 *   - Movement queue cleared
 *   - state = DISCONNECTED
 *   - Player invisible to other players
 * 
 * COMPLEXITY: O(n) time where n = movement.waypoint_count
 */
void player_disconnect(Player* player);

/*
 * player_set_socket - Assign socket to player and mark connected
 * 
 * @param player     Pointer to Player
 * @param socket_fd  TCP socket file descriptor (>= 0)
 * 
 * ALGORITHM:
 *   1. Set player->socket_fd = socket_fd
 *   2. Set player->state = PLAYER_STATE_CONNECTED
 * 
 * STATE TRANSITION:
 *   DISCONNECTED → CONNECTED
 * 
 * WHEN CALLED:
 *   After accept() returns new client connection:
 *     int client_fd = accept(server_fd, &addr, &addr_len);
 *     Player* player = find_free_player_slot();
 *     player_set_socket(player, client_fd);
 * 
 * PRECONDITIONS:
 *   - socket_fd >= 0 (valid file descriptor)
 *   - player->state == DISCONNECTED (slot available)
 * 
 * POSTCONDITIONS:
 *   - player->socket_fd == socket_fd
 *   - player->state == CONNECTED
 *   - Player ready to receive login packet
 * 
 * EXAMPLE - ACCEPT LOOP:
 *   while (running) {
 *     int fd = accept(server_socket, NULL, NULL);
 *     if (fd >= 0) {
 *       for (u32 i = 0; i < MAX_PLAYERS; i++) {
 *         if (players[i].state == PLAYER_STATE_DISCONNECTED) {
 *           player_set_socket(&players[i], fd);
 *           printf("Player %u connected (fd=%d)\n", i, fd);
 *           break;
 *         }
 *       }
 *     }
 *   }
 * 
 * COMPLEXITY: O(1) time
 */
void player_set_socket(Player* player, i32 socket_fd);

/*
 * player_set_position - Update player position and check region change
 * 
 * @param player  Pointer to Player
 * @param x       New world X coordinate
 * @param z       New world Z coordinate
 * @param height  New height plane [0-3]
 * 
 * ALGORITHM:
 *   1. Get old region: old_region_x = position_get_region_x(&player->position)
 *                      old_region_z = position_get_region_z(&player->position)
 *   2. Set new position: position_init(&player->position, x, z, height)
 *   3. Get new region: new_region_x = position_get_region_x(&player->position)
 *                      new_region_z = position_get_region_z(&player->position)
 *   4. If region changed: player->region_changed = true
 *   5. Set needs_placement = true (force position update)
 * 
 * REGION CALCULATION:
 *   Region coordinates = world coordinates >> 6
 *   Example:
 *     Position (3222, 3218) → Region (50, 50)
 *     Position (3200, 3200) → Region (50, 50)  ← same region
 *     Position (3264, 3200) → Region (51, 50)  ← different region!
 * 
 * REGION CHANGE DETECTION:
 *   ┌────────────────────────────────────────────────────┐
 *   │ Old Pos   New Pos     Region Change?   Action      │
 *   ├────────────────────────────────────────────────────┤
 *   │ (3200,   (3201,       50,50 → 50,50    No change   │
 *   │  3200)    3200)                                    │
 *   ├────────────────────────────────────────────────────┤
 *   │ (3263,   (3264,       50,50 → 51,50    CHANGED     │
 *   │  3200)    3200)                         Send map!  │
 *   └────────────────────────────────────────────────────┘
 * 
 * WHY needs_placement = true?
 *   Teleport causes immediate position jump (not incremental movement)
 *   Server must send absolute coordinates instead of delta
 *   Example: Walk sends "move 1 tile east", teleport sends "position is (3200,3200)"
 * 
 * USAGE - TELEPORT SPELL:
 *   void teleport_lumbridge(Player* player) {
 *     movement_reset(&player->movement);  // Cancel queued path
 *     player_set_position(player, 3222, 3218, 0);
 *     player->teleporting = true;  // Play teleport animation
 *   }
 * 
 * USAGE - RESPAWN AFTER DEATH:
 *   void respawn_player(Player* player) {
 *     player_set_position(player, 3222, 3218, 0);  // Lumbridge
 *     player->update_flags |= UPDATE_FLAG_APPEARANCE;  // Reset appearance
 *   }
 * 
 * SIDE EFFECTS:
 *   - player->position updated
 *   - region_changed may be set to true
 *   - needs_placement always set to true
 *   - Next tick sends full position update
 * 
 * COMPLEXITY: O(1) time
 */
void player_set_position(Player* player, u32 x, u32 z, u32 height);

/*
 * player_process_movement - Process queued waypoints and update position
 * 
 * @param player  Pointer to Player
 * 
 * ALGORITHM:
 *   1. Reset directions: primary_direction = secondary_direction = -1
 *   2. If movement queue empty: return (no movement this tick)
 *   3. Store old region coordinates
 *   4. WALK STEP:
 *        a. Get waypoint: walk_point = movement_get_next(&player->movement)
 *        b. If valid direction: update position, set primary_direction
 *   5. RUN STEP (if running):
 *        a. Get waypoint: run_point = movement_get_next(&player->movement)
 *        b. If valid direction: update position, set secondary_direction
 *   6. Check region change: if region changed, set flag and send map data
 *   7. Free waypoint memory: free(walk_point), free(run_point)
 * 
 * MOVEMENT PROCESSING:
 *   Walking: 1 tile per tick (600ms)
 *     primary_direction set, secondary_direction = -1
 *   
 *   Running: 2 tiles per tick (600ms)
 *     primary_direction set (first tile)
 *     secondary_direction set (second tile)
 * 
 * DIRECTION ENCODING:
 *   Directions stored as indices into delta arrays:
 *     0 = NORTHWEST  (-1, +1)
 *     1 = NORTH      ( 0, +1)
 *     2 = NORTHEAST  (+1, +1)
 *     3 = WEST       (-1,  0)
 *     4 = EAST       (+1,  0)
 *     5 = SOUTHWEST  (-1, -1)
 *     6 = SOUTH      ( 0, -1)
 *     7 = SOUTHEAST  (+1, -1)
 *    -1 = NONE       (not moving)
 * 
 * EXAMPLE - WALKING NORTHEAST:
 *   Initial: position = (3200, 3200)
 *   Queue: [(3201, 3201, dir=NORTHEAST)]
 *   
 *   player_process_movement():
 *     walk_point = movement_get_next()  → {3201, 3201, NORTHEAST}
 *     position_move(&position, +1, +1)  → position = (3201, 3201)
 *     primary_direction = NORTHEAST (2)
 *     
 *   Result: Player moved northeast, direction=2 sent in update packet
 * 
 * EXAMPLE - RUNNING SOUTHEAST:
 *   Initial: position = (3200, 3200), running = true
 *   Queue: [(3201, 3199, SE), (3202, 3198, SE)]
 *   
 *   player_process_movement():
 *     walk_point = {3201, 3199, SE}
 *     position = (3201, 3199)
 *     primary_direction = SE (7)
 *     
 *     run_point = {3202, 3198, SE}
 *     position = (3202, 3198)
 *     secondary_direction = SE (7)
 *   
 *   Result: Player ran 2 tiles southeast in one tick
 * 
 * REGION CHANGE HANDLING:
 *   If player crosses region boundary:
 *     1. Set region_changed = true
 *     2. Print debug: "Player region changed from (X,Y) to (X',Y')"
 *     3. Call map_send_load_area() to send new map chunks
 * 
 * DEBUG OUTPUT:
 *   Walk: "DEBUG: walk_point x=3201, z=3200, dir=4"
 *   Run:  (same for run_point)
 *   Region: "DEBUG: Player region changed from (50,50) to (51,50)"
 * 
 * WHEN CALLED:
 *   Every game tick (600ms) in main server loop:
 *     for each player in LOGGED_IN state:
 *       player_process_movement(&player);
 * 
 * MEMORY MANAGEMENT:
 *   movement_get_next() returns malloc'd Point
 *   Must free() both walk_point and run_point at end
 *   NULL checks prevent double-free if queue empty
 * 
 * COMPLEXITY: O(n) time where n = movement.waypoint_count (due to dequeue shift)
 */
void player_process_movement(Player* player);

/*
 * player_is_active - Check if player is logged in
 * 
 * @param player  Pointer to Player
 * @return        true if state == LOGGED_IN, false otherwise
 * 
 * USAGE:
 *   Main game loop:
 *     for (u32 i = 0; i < MAX_PLAYERS; i++) {
 *       if (player_is_active(&players[i])) {
 *         player_process_movement(&players[i]);
 *         send_player_update(&players[i]);
 *       }
 *     }
 * 
 * EQUIVALENT TO:
 *   return player->state == PLAYER_STATE_LOGGED_IN;
 * 
 * WHY FUNCTION INSTEAD OF DIRECT CHECK?
 *   - Abstraction (state implementation may change)
 *   - Readability (clear intent)
 *   - Future extension (may check additional conditions)
 * 
 * COMPLEXITY: O(1) time
 */
bool player_is_active(const Player* player);

#endif /* PLAYER_H */
