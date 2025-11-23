/*******************************************************************************
 * SERVER.C - RuneScape Game Server Implementation
 *******************************************************************************
 * 
 * This file implements the core game server loop and packet processing for
 * a RuneScape private server (revision 225, May 2004 protocol).
 * 
 * KEY ALGORITHMS IMPLEMENTED:
 *   1. Fixed time-step game loop (600ms ticks)
 *   2. Non-blocking packet processing
 *   3. Variable-length packet parsing with ISAAC decryption
 *   4. Delta-encoded movement path reconstruction
 *   5. Player slot allocation
 * 
 * EDUCATIONAL CONCEPTS:
 *   - Event-driven architecture
 *   - Network protocol design
 *   - Packet framing and buffering
 *   - Game tick synchronization
 *   - Resource management (player slots)
 * 
 * MAIN LOOP ARCHITECTURE:
 * 
 *   The server uses a hybrid approach combining:
 *   - High-frequency polling (1ms) for network I/O
 *   - Low-frequency tick processing (600ms) for game logic
 * 
 *   This design ensures:
 *   - Responsive network handling (no queuing delays)
 *   - Deterministic game state (fixed time steps)
 *   - Efficient CPU usage (brief sleep prevents spinning)
 * 
 *   ┌──────────────────────────────────────────────────────────┐
 *   │ MAIN LOOP (1ms iterations)                               │
 *   │                                                          │
 *   │  while (server->running) {                               │
 *   │      current_time = get_time();                          │
 *   │      elapsed = current_time - last_tick_time;            │
 *   │                                                          │
 *   │      if (elapsed >= 600ms) {                             │
 *   │          ┌─────────────────────────────┐                 │
 *   │          │ GAME TICK PROCESSING        │                 │
 *   │          │ - Update world state        │                 │
 *   │          │ - Process player movement   │                 │
 *   │          │ - Execute combat            │                 │
 *   │          │ - Send update packets       │                 │
 *   │          └─────────────────────────────┘                 │
 *   │          last_tick_time = current_time;                  │
 *   │      }                                                   │
 *   │                                                          │
 *   │      ┌───────────────────────────────┐                   │
 *   │      │ NETWORK PROCESSING            │                   │
 *   │      │ - Accept new connections      │                   │
 *   │      │ - Read incoming packets       │                   │
 *   │      │ - Parse and dispatch packets  │                   │
 *   │      └───────────────────────────────┘                   │
 *   │                                                          │
 *   │      usleep(1000);  // 1ms sleep                         │
 *   │  }                                                       │
 *   └──────────────────────────────────────────────────────────┘
 * 
 * PACKET PROCESSING PIPELINE:
 * 
 *   Stage 1: TCP Reception
 *   ┌─────────────────────────────────────────────────────────┐
 *   │ recv() from socket -> append to per-player input buffer │
 *   │ Buffer: [old data...][new data]                         │
 *   └─────────────────────────────────────────────────────────┘
 *                             |
 *                             v
 *   Stage 2: Login vs Game Packet Decision
 *   ┌─────────────────────────────────────────────────────────┐
 *   │ if (player->state == CONNECTED)                         │
 *   │     -> Process login handshake                          │
 *   │ else if (player->state == LOGGED_IN)                    │
 *   │     -> Parse game packets                               │
 *   └─────────────────────────────────────────────────────────┘
 *                             |
 *                             v
 *   Stage 3: Opcode Decryption
 *   ┌─────────────────────────────────────────────────────────┐
 *   │ encrypted_opcode = buffer[0]                            │
 *   │ key = isaac_get_next(player->in_cipher)                 │
 *   │ opcode = (encrypted_opcode - key) & 0xFF                │
 *   └─────────────────────────────────────────────────────────┘
 *                             |
 *                             v
 *   Stage 4: Length Determination
 *   ┌─────────────────────────────────────────────────────────┐
 *   │ length = PacketLengths[opcode]                          │
 *   │ if (length == -1)  // VAR_BYTE                          │
 *   │     length = buffer[1], header_size = 2                 │
 *   │ else if (length == -2)  // VAR_SHORT                    │
 *   │     length = (buffer[1]<<8)|buffer[2], header_size = 3  │
 *   │ else  // Fixed length                                   │
 *   │     header_size = 1                                     │
 *   └─────────────────────────────────────────────────────────┘
 *                             |
 *                             v
 *   Stage 5: Completeness Check
 *   ┌─────────────────────────────────────────────────────────┐
 *   │ if (buffer_size < header_size + length)                 │
 *   │     -> Wait for more data (partial packet)              │
 *   │ else                                                    │
 *   │     -> Extract payload, dispatch to handler             │
 *   └─────────────────────────────────────────────────────────┘
 *                             |
 *                             v
 *   Stage 6: Packet Dispatch
 *   ┌─────────────────────────────────────────────────────────┐
 *   │ switch (opcode) {                                       │
 *   │     case 93/165/181: handle_movement(...); break;       │
 *   │     case 52:  handle_player_design(...); break;         │
 *   │     case 158: handle_command(...); break;               │
 *   │     ...                                                 │
 *   │ }                                                       │
 *   └─────────────────────────────────────────────────────────┘
 *                             |
 *                             v
 *   Stage 7: Buffer Cleanup
 *   ┌─────────────────────────────────────────────────────────┐
 *   │ Remove processed packet from input buffer:              │
 *   │ memmove(buffer, buffer + packet_size, remaining)        │
 *   └─────────────────────────────────────────────────────────┘
 * 
 * MOVEMENT PACKET ENCODING:
 * 
 * RuneScape uses delta-encoded movement paths to save bandwidth.
 * Instead of sending absolute coordinates for each step, the client
 * sends a starting position + relative offsets.
 * 
 * WIRE FORMAT:
 *   [opcode][ctrl_down][start_x][start_z][dx0][dz0][dx1][dz1]...
 *   
 *   opcode:    248, 165, 181, or 93 (different movement types)
 *   ctrl_down: 1 if running, 0 if walking
 *   start_x:   16-bit absolute X coordinate (big-endian)
 *   start_z:   16-bit absolute Z coordinate (big-endian)
 *   dx, dz:    8-bit signed deltas (-128 to +127)
 * 
 * DECODING EXAMPLE:
 * 
 *   Received packet:
 *     opcode    = 248
 *     ctrl_down = 1 (running)
 *     start_x   = 3200
 *     start_z   = 3200
 *     deltas    = [+1,0], [+1,0], [+1,+1], [0,+1]
 *   
 *   Reconstructed path:
 *     Step 0: (3200, 3200)  <- starting position
 *     Step 1: (3201, 3200)  <- 3200 + 1, 3200 + 0
 *     Step 2: (3202, 3200)  <- 3201 + 1, 3200 + 0
 *     Step 3: (3203, 3201)  <- 3202 + 1, 3200 + 1
 *     Step 4: (3203, 3202)  <- 3203 + 0, 3201 + 1
 *   
 *   Player will walk/run through these coordinates in order.
 * 
 * WHY DELTA ENCODING?
 *   Bandwidth comparison for 10-step path:
 *   - Absolute: 10 * 4 bytes = 40 bytes (two 16-bit coords per step)
 *   - Delta:    4 + 10 * 2 = 24 bytes (4-byte header + 2 bytes per step)
 *   - Savings:  40% reduction
 * 
 * ISAAC CIPHER INTEGRATION:
 * 
 * The ISAAC cipher (Indirection, Shift, Accumulate, Add, Count) is a
 * cryptographically secure random number generator used for packet
 * obfuscation.
 * 
 * OPCODE ENCRYPTION:
 *   Client and server share synchronized ISAAC state (seeded during login)
 *   
 *   Client sends:
 *     encrypted_opcode = (plain_opcode + isaac_next()) & 0xFF
 *   
 *   Server decrypts:
 *     plain_opcode = (encrypted_opcode - isaac_next()) & 0xFF
 *   
 *   Both sides must call isaac_next() in lockstep!
 * 
 * SYNCHRONIZATION EXAMPLE:
 * 
 *   Initial state: Client and server both at ISAAC position 0
 *   
 *   Client sends packet 1:
 *     key[0] = isaac_next() = 0x42
 *     encrypted = (184 + 0x42) & 0xFF = 0xF0
 *   
 *   Server receives packet 1:
 *     key[0] = isaac_next() = 0x42  (same!)
 *     decrypted = (0xF0 - 0x42) & 0xFF = 184
 *   
 *   Client sends packet 2:
 *     key[1] = isaac_next() = 0x7A  (next value)
 *     encrypted = (52 + 0x7A) & 0xFF = 0xAE
 *   
 *   Server receives packet 2:
 *     key[1] = isaac_next() = 0x7A  (synchronized)
 *     decrypted = (0xAE - 0x7A) & 0xFF = 52
 * 
 * DESYNCHRONIZATION BUGS:
 *   If server calls isaac_next() when it shouldn't (or vice versa),
 *   all future packets will decrypt incorrectly. This is why login
 *   handshake must initialize ciphers identically on both sides.
 * 
 * TICK TIMING ANALYSIS:
 * 
 * The 600ms tick rate is achieved using high-resolution timers:
 * 
 *   clock_gettime(CLOCK_MONOTONIC, &time)
 *   
 *   CLOCK_MONOTONIC guarantees:
 *   - Never jumps backwards (unlike CLOCK_REALTIME with NTP)
 *   - Nanosecond precision (1 billionth of a second)
 *   - Unaffected by system time changes
 * 
 * DRIFT CORRECTION:
 * 
 *   Bad approach (accumulates drift):
 *     while (running) {
 *         server_tick();
 *         sleep(600ms);  // Tick takes 50ms, actual rate is 650ms!
 *     }
 *   
 *   Good approach (compensates for processing time):
 *     last_tick = get_time();
 *     while (running) {
 *         current = get_time();
 *         elapsed = current - last_tick;
 *         if (elapsed >= 600ms) {
 *             server_tick();
 *             last_tick = current;  // Reset to actual time
 *         }
 *     }
 * 
 * PERFORMANCE METRICS:
 * 
 * Target tick budget breakdown (600ms total):
 *   - Player movement processing:    50ms  (8.3%)
 *   - Player update packet building: 200ms (33.3%)
 *   - NPC update packet building:    100ms (16.7%)
 *   - Combat calculations:           50ms  (8.3%)
 *   - World processing:              50ms  (8.3%)
 *   - Miscellaneous:                 50ms  (8.3%)
 *   - Safety margin:                 100ms (16.7%)
 *   Total:                           600ms (100%)
 * 
 * If tick exceeds 600ms:
 *   - Next tick starts immediately (no sleep)
 *   - Server is "lagging" - visible to players
 *   - Actions queue up, world becomes unresponsive
 *   - Solution: Optimize slow code or reduce player cap
 * 
 ******************************************************************************/

#ifndef _WIN32
#define _POSIX_C_SOURCE 199309L
#endif

#include "server.h"
#include "buffer.h"
#include "login.h"
#include "update.h"
#include "world.h"
#include "map.h"
#include "packets.h"
#include "constants.h"
#include "server_packets.h"
#include "cache.h"
#include "item.h"
#include "npc.h"
#include "object.h"
#include "player_save.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/*
 * Platform-specific includes for timing and sleep functions
 */
#ifdef _WIN32
#include <windows.h>

/*
 * win_clock_gettime - Windows implementation of POSIX clock_gettime
 * 
 * ALGORITHM:
 *   1. Query performance counter frequency (ticks per second)
 *   2. Query current performance counter value
 *   3. Convert to seconds and nanoseconds
 * 
 * PRECISION:
 *   QueryPerformanceCounter typically has microsecond precision
 *   (much better than GetTickCount's 15ms resolution)
 * 
 * COMPLEXITY: O(1) time
 */
static int win_clock_gettime(int clk_id, struct timespec* tp) {
    (void)clk_id;
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    tp->tv_sec = count.QuadPart / freq.QuadPart;
    tp->tv_nsec = (count.QuadPart % freq.QuadPart) * 1000000000 / freq.QuadPart;
    return 0;
}

#define usleep(x) Sleep((x)/1000)
#define clock_gettime(clk_id, tp) win_clock_gettime(clk_id, tp)
#define CLOCK_MONOTONIC 1
#else
#include <unistd.h>
#endif

/*
 * Forward declarations for internal helper functions
 * 
 * These functions are static (file-scoped) to prevent namespace pollution.
 */
static void server_handle_packet(Player* player, u8 opcode, StreamBuffer* buf, u32 packet_length);
static void server_handle_movement_packet(Player* player, StreamBuffer* buf, u32 packet_length, u8 opcode);
static void server_handle_player_design(Player* player, StreamBuffer* buf);
static void server_handle_if_button(Player* player, StreamBuffer* buf);
static void server_handle_command(Player* player, StreamBuffer* buf, u32 packet_length);
static void server_send_initial_game_packets(Player* player);

/*******************************************************************************
 * SERVER LIFECYCLE MANAGEMENT
 ******************************************************************************/

/*
 * server_init - Initialize game server and all subsystems
 * 
 * INITIALIZATION ORDER:
 *   1. World (must exist before anything can reference it)
 *   2. Cache (needed for loading definitions)
 *   3. Item system (uses cache definitions)
 *   4. NPC system (uses cache definitions)
 *   5. Object system (uses cache definitions)
 *   6. Player slots (independent of above)
 *   7. Network socket (last, so we don't accept connections until ready)
 * 
 * ERROR HANDLING:
 *   Uses early-return pattern with cleanup on failure
 *   Each subsystem checks previous dependencies before initializing
 * 
 * EXAMPLE FAILURE CASE:
 *   If cache_init fails:
 *   1. Print error message
 *   2. Destroy world (created before cache)
 *   3. Return false (caller knows init failed)
 *   
 *   Caller must not call server_run() after init failure!
 * 
 * COMPLEXITY: O(1) time (all subsystems have constant init time)
 */
bool server_init(GameServer* server, u16 port) {
    printf("Initializing server..\n");
    
    /* Zero-initialize entire server structure */
    memset(server, 0, sizeof(GameServer));
    
    /* Initialize world - central game state container */
    printf("Creating world...\n");
    g_world = world_create();
    if (!g_world) {
        fprintf(stderr, "ERROR: Failed to create world\n");
        return false;
    }
    
    /* Initialize cache - loads item/NPC/object definitions from disk */
    printf("Creating cache system...\n");
    g_cache = cache_create();
    if (!g_cache) {
        fprintf(stderr, "ERROR: Failed to create cache\n");
        world_destroy(g_world);
        return false;
    }
    if (!cache_init(g_cache, "data")) {
        fprintf(stderr, "WARNING: Cache initialization had issues\n");
    }
    
    /* Initialize item system - manages item definitions and spawns */
    printf("Creating item system...\n");
    g_items = item_system_create();
    if (g_items) {
        if (!item_system_init(g_items)) {
            fprintf(stderr, "WARNING: Item system initialization failed\n");
        }
    } else {
        fprintf(stderr, "WARNING: Failed to create item system\n");
    }
    
    /* Initialize NPC system - manages spawns and AI */
    printf("Creating NPC system...\n");
    g_npcs = npc_system_create(MAX_NPCS);
    if (g_npcs) {
        if (!npc_system_init(g_npcs)) {
            fprintf(stderr, "WARNING: NPC system initialization failed\n");
        }
        
        /* Spawn test NPCs in Lumbridge (starting area) */
        printf("Spawning test NPCs...\n");
        npc_spawn(g_npcs, 0, 3222, 3218, 0);  /* Hans (NPC ID 0) */
        npc_spawn(g_npcs, 1, 3220, 3220, 0);  /* Man (NPC ID 1) */
    } else {
        fprintf(stderr, "WARNING: Failed to create NPC system\n");
    }
    
    /* Initialize object system - manages scenery and interactive objects */
    printf("Creating object system...\n");
    g_objects = object_system_create(MAX_GROUND_ITEMS);
    if (g_objects) {
        if (!object_system_init(g_objects)) {
            fprintf(stderr, "WARNING: Object system initialization failed\n");
        }
        
        /* Spawn test objects in Lumbridge */
        printf("Spawning test objects...\n");
        object_spawn(g_objects, 1519, 3220, 3210, 0, OBJECT_TYPE_WALL, 0);  /* Door */
        object_spawn(g_objects, 1276, 3225, 3225, 0, OBJECT_TYPE_INTERACTABLE, 0);  /* Tree */
    } else {
        fprintf(stderr, "WARNING: Failed to create object system\n");
    }
    
    /* Initialize all player slots to disconnected state */
    printf("Initializing %d player slots...\n", MAX_PLAYERS);
    for (u32 i = 0; i < MAX_PLAYERS; i++) {
        player_init(&server->players[i], i);
    }
    
    /* Initialize network - create TCP listen socket */
    printf("Initializing network on port %u...\n", port);
    if (!network_init(&server->network, port)) {
        fprintf(stderr, "ERROR: Failed to initialize network on port %u\n", port);
        world_destroy(g_world);
        return false;
    }
    
    /* Mark server as running and reset tick counter */
    server->running = true;
    server->tick_count = 0;
    
    printf("Server initialization complete!\n");
    return true;
}

/*
 * server_shutdown - Cleanup and deallocate all server resources
 * 
 * SHUTDOWN ORDER:
 *   Reverse of initialization to prevent use-after-free bugs
 *   
 *   1. Stop accepting new connections (set running = false)
 *   2. Disconnect all players (save data, close sockets)
 *   3. Close network socket
 *   4. Destroy objects (references world data)
 *   5. Destroy NPCs (references world data)
 *   6. Destroy items (references world data)
 *   7. Destroy cache (definitions no longer needed)
 *   8. Destroy world (last, as everything references it)
 * 
 * GRACEFUL DISCONNECTION:
 *   Each player receives logout packet before socket close
 *   This allows client to show "You have been logged out" message
 *   instead of "Connection lost" error
 * 
 * MEMORY LEAK PREVENTION:
 *   All subsystems set their global pointers to NULL after destroy
 *   This prevents use-after-free if shutdown is called twice
 * 
 * COMPLEXITY: O(N) where N = number of connected players
 */
void server_shutdown(GameServer* server) {
    server->running = false;
    
    /* Disconnect all players - iterate all slots */
    for (u32 i = 0; i < MAX_PLAYERS; i++) {
        if (server->players[i].state != PLAYER_STATE_DISCONNECTED) {
            player_disconnect(&server->players[i]);
        }
    }
    
    /* Shutdown network - close listen socket */
    network_shutdown(&server->network);
    
    /* Destroy subsystems in reverse initialization order */
    if (g_objects) {
        object_system_destroy(g_objects);
        g_objects = NULL;
    }
    
    if (g_npcs) {
        npc_system_destroy(g_npcs);
        g_npcs = NULL;
    }
    
    if (g_items) {
        item_system_destroy(g_items);
        g_items = NULL;
    }
    
    if (g_cache) {
        cache_destroy(g_cache);
        g_cache = NULL;
    }
    
    if (g_world) {
        world_destroy(g_world);
        g_world = NULL;
    }
}

/*******************************************************************************
 * MAIN GAME LOOP
 ******************************************************************************/

/*
 * server_run - Main event loop (runs until shutdown)
 * 
 * LOOP STRUCTURE:
 * 
 *   Initialize timing:
 *     last_tick_time = current_time
 *   
 *   Main loop:
 *     while (server->running) {
 *         current_time = get_monotonic_time()
 *         elapsed = current_time - last_tick_time
 *         
 *         if (elapsed >= TICK_RATE_MS) {
 *             Execute game tick
 *             last_tick_time = current_time
 *         }
 *         
 *         Process new connections (non-blocking)
 *         Process incoming packets (non-blocking)
 *         
 *         Sleep 1ms (prevent CPU spinning)
 *     }
 * 
 * TIME CALCULATION:
 * 
 *   struct timespec has two fields:
 *     tv_sec:  seconds (64-bit integer)
 *     tv_nsec: nanoseconds (0-999999999)
 *   
 *   To convert to milliseconds:
 *     ms = (sec_diff * 1000) + (nsec_diff / 1000000)
 *   
 *   Example:
 *     last_tick:   {tv_sec=1000, tv_nsec=500000000}  (1000.5 sec)
 *     current:     {tv_sec=1001, tv_nsec=200000000}  (1001.2 sec)
 *     sec_diff:    1001 - 1000 = 1 sec
 *     nsec_diff:   200000000 - 500000000 = -300000000 nsec
 *     elapsed_ms:  (1 * 1000) + (-300000000 / 1000000)
 *                = 1000 + (-300)
 *                = 700 ms
 * 
 * TICK RATE PRECISION:
 * 
 *   With 1ms loop granularity and 600ms tick rate:
 *   - Best case: exactly 600ms (tick fires immediately)
 *   - Worst case: 601ms (tick fires 1ms late)
 *   - Error: +/- 1ms = 0.16% deviation
 *   
 *   This is acceptable for game logic (players cannot perceive <10ms)
 * 
 * COMPLEXITY: Infinite loop (until shutdown)
 */
void server_run(GameServer* server) {
    struct timespec last_tick, current_time;
    clock_gettime(CLOCK_MONOTONIC, &last_tick);
    
    printf("Server running on port %u...\n", server->network.port);
    
    while (server->running) {
        /* Get current time using monotonic clock (never jumps backwards) */
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        
        /* Calculate elapsed time since last tick in milliseconds */
        long elapsed_ms = (current_time.tv_sec - last_tick.tv_sec) * 1000 +
                         (current_time.tv_nsec - last_tick.tv_nsec) / 1000000;
        
        /* Process game tick if 600ms elapsed */
        if (elapsed_ms >= TICK_RATE_MS) {
            server_tick(server);
            last_tick = current_time;  /* Reset tick timer */
        }
        
        /* Process network events (non-blocking, returns immediately) */
        server_process_connections(server);
        server_process_packets(server);
        
        /* Sleep briefly to prevent CPU spinning at 100% */
        usleep(1000);  /* 1 millisecond = 1000 microseconds */
    }
}

/*
 * server_tick - Process one game tick (600ms cycle)
 * 
 * TICK PROCESSING:
 *   1. Increment global tick counter (used for timed events)
 *   2. Delegate to world_process() for main game logic:
 *      - Player movement updates
 *      - Player visibility calculations
 *      - NPC AI and movement
 *      - Combat processing
 *      - Item spawn/despawn timers
 *      - Send player/NPC update packets
 * 
 * TICK COUNTER USES:
 *   - Periodic events: if (tick_count % 100 == 0) -> every minute
 *   - Spawn timers: spawn_tick + 50 -> respawn after 30 seconds
 *   - Combat cooldowns: last_attack_tick + 4 -> attack every 2.4 sec
 * 
 * OVERFLOW HANDLING:
 *   u64 tick_count wraps after 2^64 ticks
 *   At 600ms per tick: 2^64 * 0.6 sec = 351 trillion years
 *   Safe to ignore overflow in practice
 * 
 * COMPLEXITY: O(N * M) where:
 *   N = number of players
 *   M = average number of visible entities per player
 */
void server_tick(GameServer* server) {
    server->tick_count++;
    
    /* Process world state - delegates to world.c */
    if (g_world) {
        world_process(g_world);
    }
}

/*******************************************************************************
 * CONNECTION HANDLING
 ******************************************************************************/

/*
 * server_process_connections - Accept pending connection (non-blocking)
 * 
 * ALGORITHM:
 *   1. Try to accept connection (returns immediately if none pending)
 *   2. If no connection, return
 *   3. Find free player slot using linear search
 *   4. If server full, close socket immediately
 *   5. Assign socket to player slot
 *   6. Begin login handshake
 * 
 * NON-BLOCKING ACCEPT:
 *   Socket is configured with O_NONBLOCK flag
 *   accept() returns -1 with errno=EWOULDBLOCK if no connection pending
 *   This prevents blocking the main loop waiting for connections
 * 
 * CAPACITY HANDLING:
 *   When all MAX_PLAYERS slots are occupied:
 *   - New connection is closed immediately
 *   - No data is sent to client (just TCP FIN)
 *   - Client sees "Connection refused" or timeout
 *   - Alternative: Send "Server full" message before closing
 * 
 * SLOT ALLOCATION:
 *   Uses server_find_free_slot() which does linear search
 *   Optimization: Maintain free-list for O(1) allocation
 *   
 *   Free-list approach:
 *     int free_slots[MAX_PLAYERS];  // Stack of free indices
 *     int free_count = MAX_PLAYERS;
 *     
 *     On disconnect: free_slots[free_count++] = player_index
 *     On connect:    player_index = free_slots[--free_count]
 *   
 *   Trade-off: 8KB memory for O(1) vs O(N) allocation
 * 
 * COMPLEXITY: O(N) where N = MAX_PLAYERS (due to find_free_slot)
 */
void server_process_connections(GameServer* server) {
    /* Try to accept connection (non-blocking) */
    i32 client_fd = network_accept_connection(&server->network);
    if (client_fd < 0) {
        /* No pending connection or accept failed */
        return;
    }
    
    /* Find free player slot */
    Player* player = server_find_free_slot(server);
    if (player) {
        /* Slot available - assign socket and start login */
        player_set_socket(player, client_fd);
        login_process_connection(player);
        printf("Player connected: index=%u fd=%d\n", player->index, client_fd);
    } else {
        /* Server full - reject connection */
        network_close_socket(client_fd);
        printf("Server full, rejected connection\n");
    }
}

/*******************************************************************************
 * PACKET PROCESSING
 ******************************************************************************/

/*
 * server_process_packets - Read and dispatch packets from all players
 * 
 * ALGORITHM:
 *   For each player slot:
 *     1. Skip if socket is invalid (player disconnected)
 *     2. Try to recv() data into temporary buffer (non-blocking)
 *     3. If recv() > 0: append to player's input buffer
 *     4. If recv() == 0: player disconnected (graceful close)
 *     5. If recv() < 0: error or EWOULDBLOCK (no data available)
 *     6. Process login handshake if player is in CONNECTED state
 *     7. Parse and dispatch game packets if player is LOGGED_IN
 * 
 * INPUT BUFFERING:
 *   Each player has in_buffer[MAX_PACKET_SIZE] to accumulate data
 *   Packets may arrive in fragments across multiple recv() calls
 *   
 *   Example fragmentation:
 *     Packet: [opcode=248][len=15][...payload...]  (17 bytes total)
 *     
 *     recv() call 1: Returns 10 bytes
 *       in_buffer = [opcode][len][...8 bytes...]
 *       in_buffer_size = 10
 *       Packet incomplete, wait for more data
 *     
 *     recv() call 2: Returns 7 bytes
 *       in_buffer = [opcode][len][...15 bytes...]
 *       in_buffer_size = 17
 *       Packet complete, process and remove from buffer
 * 
 * PACKET LENGTH LOOKUP:
 *   PacketLengths[256] table maps opcode to payload size:
 *     >= 0: Fixed length (e.g., PacketLengths[0] = 0)
 *     -1:   Variable byte length (next byte is length)
 *     -2:   Variable short length (next 2 bytes are length)
 *   
 *   Example opcodes:
 *     PacketLengths[248] = -1   (movement packet, VAR_BYTE)
 *     PacketLengths[52]  = 13   (player design, 13 bytes)
 *     PacketLengths[158] = -1   (command packet, VAR_BYTE)
 * 
 * OPCODE DECRYPTION:
 *   After login, all opcodes are encrypted with ISAAC cipher:
 *     encrypted_opcode = buffer[0]
 *     key = isaac_get_next(&player->in_cipher)
 *     opcode = (encrypted_opcode - key) & 0xFF
 *   
 *   & 0xFF ensures result is 0-255 (handles negative wrap-around)
 * 
 * HEADER SIZE CALCULATION:
 *   Fixed length:  header_size = 1 (just opcode)
 *   VAR_BYTE:      header_size = 2 (opcode + length byte)
 *   VAR_SHORT:     header_size = 3 (opcode + length short)
 * 
 * BUFFER MANAGEMENT:
 *   After processing packet, remaining data is moved to buffer start:
 *     memmove(buffer, buffer + consumed, remaining)
 *   
 *   memmove() (not memcpy!) handles overlapping memory:
 *     Before: [packet1][packet2][packet3]...
 *     After:  [packet2][packet3]...
 * 
 * COMPLEXITY: O(N * P) where:
 *   N = number of players
 *   P = average packets per player (typically 1-10 per call)
 */
void server_process_packets(GameServer* server) {
    for (u32 i = 0; i < MAX_PLAYERS; i++) {
        Player* player = &server->players[i];
        
        /* Skip disconnected players */
        if (player->socket_fd < 0) continue;
        
        /* Try to receive data (non-blocking) - loop until EWOULDBLOCK */
        u8 temp_buffer[MAX_PACKET_SIZE];
        i32 bytes_read;
        
        /* Keep reading until no more data available */
        int recv_count = 0;
        bool connection_closed = false;
        
        bytes_read = network_receive(player->socket_fd, temp_buffer, sizeof(temp_buffer));
        
        while (bytes_read > 0) {
            recv_count++;
            printf("DEBUG: recv() call #%d - Received %d bytes from player %s, hex dump:\n  ", 
                   recv_count, (int)bytes_read, player->username);
            for (int i = 0; i < bytes_read && i < 32; i++) {
                printf("%02X ", temp_buffer[i]);
                if ((i + 1) % 16 == 0 && i + 1 < bytes_read) printf("\n  ");
            }
            printf("\n");
            /* Data received - append to input buffer */
            if (player->in_buffer_size + bytes_read < MAX_PACKET_SIZE) {
                memcpy(player->in_buffer + player->in_buffer_size, temp_buffer, bytes_read);
                player->in_buffer_size += bytes_read;
                printf("DEBUG: in_buffer_size now %u after append (total recv calls: %d)\n", player->in_buffer_size, recv_count);
            }
            
            /* Try to recv more data */
            bytes_read = network_receive(player->socket_fd, temp_buffer, sizeof(temp_buffer));
        }
        
        /* Check if connection was closed (recv returned 0) */
        if (bytes_read == 0) {
            printf("Player '%s' disconnected (connection closed)\n", player->username);
            connection_closed = true;
        }
        
        if (recv_count > 0) {
            printf("DEBUG: Finished recv loop after %d successful recv() calls, final buffer size=%u\n", recv_count, player->in_buffer_size);
        }
        
        /* Process all buffered data if any was received */
        if (player->in_buffer_size > 0) {
            
            /* Process login handshake if player is connecting */
            if (player->state == PLAYER_STATE_CONNECTED && player->in_buffer_size >= 2) {
                StreamBuffer* in = buffer_create(player->in_buffer_size);
                buffer_write_bytes(in, player->in_buffer, player->in_buffer_size);
                buffer_set_position(in, 0);
                
                if (login_process_header(player, in)) {
                    /* Login successful - send initial game state */
                    server_send_initial_game_packets(player);
                    player->in_buffer_size = 0;
                }
                
                buffer_destroy(in);
            }
            
            /* Process game packets if player is logged in */
            while (player->in_buffer_size >= 1 && player->state == PLAYER_STATE_LOGGED_IN) {
                /* Decrypt opcode using ISAAC cipher */
                u8 encrypted_opcode = player->in_buffer[0];
                u8 opcode = encrypted_opcode;
                if (player->in_cipher.initialized) {
                    u32 isaac_key = isaac_get_next(&player->in_cipher);
                    opcode = (encrypted_opcode - isaac_key) & 0xFF;
                    printf("DEBUG ISAAC decrypt: encrypted=0x%02X - isaac_key=%u = opcode=%u\n", 
                           encrypted_opcode, isaac_key, opcode);
                }
                
                /* Lookup packet length from table */
                i32 packet_length = PacketLengths[opcode];
                i32 header_size = 1;
                
                if (packet_length == -1) {
                    /* VAR_BYTE: next byte is payload length */
                    if (player->in_buffer_size < 2) break;  /* Wait for length byte */
                    packet_length = player->in_buffer[1] & 0xFF;
                    header_size = 2;
                } else if (packet_length == -2) {
                    /* VAR_SHORT: next 2 bytes are payload length (big-endian) */
                    if (player->in_buffer_size < 3) break;  /* Wait for length bytes */
                    packet_length = ((player->in_buffer[1] & 0xFF) << 8) | (player->in_buffer[2] & 0xFF);
                    header_size = 3;
                }
                
                /* Ensure length is non-negative */
                if (packet_length < 0) packet_length = 0;
                
                /* Check if full packet received */
                if (player->in_buffer_size < header_size + packet_length) {
                    break;  /* Partial packet, wait for more data */
                }
                
                /* Create buffer for packet payload */
                StreamBuffer* buf = buffer_create(packet_length);
                buffer_write_bytes(buf, player->in_buffer + header_size, packet_length);
                buffer_set_position(buf, 0);
                
                /* Dispatch to packet handler */
                server_handle_packet(player, opcode, buf, packet_length);
                
                buffer_destroy(buf);
                
                /* Remove packet from input buffer (move remaining data forward) */
                u32 total_size = header_size + packet_length;
                memmove(player->in_buffer, player->in_buffer + total_size, 
                       player->in_buffer_size - total_size);
                player->in_buffer_size -= total_size;
            }
        }
        
        /* Check if connection was closed during recv loop */
        if (connection_closed) {
            /* Connection closed gracefully */
            printf("Player '%s' disconnected (connection closed)\n", player->username);
            player_disconnect(player);
            continue;  /* Skip to next player */
        }
    }
}

/*******************************************************************************
 * PACKET HANDLERS
 ******************************************************************************/

/*
 * server_handle_packet - Dispatch packet to appropriate handler
 * 
 * @param player         Player who sent the packet
 * @param opcode         Decrypted opcode (0-255)
 * @param buf            Payload buffer (position=0, ready to read)
 * @param packet_length  Payload size in bytes
 * 
 * PACKET CATEGORIES:
 * 
 *   Movement (248, 165, 181, 93):
 *     Different opcodes indicate walk vs run, queue vs replace
 *     All contain delta-encoded path data
 *   
 *   Player Design (52):
 *     Sent when player customizes appearance
 *     Contains gender, body parts, colors
 *   
 *   Map Requests (150, 81):
 *     Request map region data for specific areas
 *     Server responds with terrain and object data
 *   
 *   Commands (158):
 *     Player-typed commands like "::tele 3200 3200 0"
 *     Used for admin/debug features
 *   
 *   Chat (108, 70, 85, ...):
 *     Public chat, private messages, clan chat
 *     Currently stubbed (skip payload)
 *   
 *   Items/Objects (175, 155, 31, ...):
 *     Drop item, pickup item, use item, etc.
 *     Currently stubbed (skip payload)
 *   
 *   Interfaces (194, 8, 27, ...):
 *     Click button, close interface, etc.
 *     Currently stubbed (skip payload)
 * 
 * STUB HANDLERS:
 *   Many packets are not yet implemented, so they just skip payload
 *   This prevents desynchronization (important for ISAAC cipher!)
 *   
 *   Example: buffer_skip(buf, packet_length)
 *     Advances read position without processing data
 *     Ensures buffer is fully consumed
 * 
 * DEBUG LOGGING:
 *   Prints every packet opcode and length
 *   Useful for protocol reverse engineering
 *   Can be disabled in production for performance
 * 
 * COMPLEXITY: O(1) for most handlers, O(N) for movement (N = path length)
 */
static void server_handle_packet(Player* player, u8 opcode, StreamBuffer* buf, u32 packet_length) {
    static u32 movement_packet_count = 0;
    if (opcode == 165 || opcode == 181 || opcode == 93) {
        movement_packet_count++;
        printf("[RX] MOVEMENT PACKET #%u: op=%u len=%d\n", movement_packet_count, (unsigned)opcode, (int)packet_length);
    } else {
        printf("[RX] op=%u len=%d\n", (unsigned)opcode, (int)packet_length);
    }

    switch (opcode) {
        /* Movement packets (different types for walk/run) */
        case 165: case 181: case 93:
            server_handle_movement_packet(player, buf, packet_length, opcode);
            break;

        /* Player appearance design */
        case 52:
            server_handle_player_design(player, buf);
            break;

        /* Map region requests */
        case 150: case 81:
            map_handle_request(player, buf, packet_length);
            break;

        /* Keepalive and misc packets */
        case 224:
        case 38:
        case 0:
            buffer_skip(buf, packet_length);
            break;

        case 79:
            buffer_skip(buf, packet_length);
            break;

        case 30:
            printf("Player '%s' requested logout (idle timer)\n", player->username);
            player_disconnect(player);
            return;
        
        /* Chat packets (public, private, clan, etc.) */
        case 108: case 70: case 85: case 215: case 236:
        case 146: case 219: case 233: case 220: case 238:
        case 17:  case 88: case 176: case 7:   case 66:
        case 2:
            buffer_skip(buf, packet_length);
            break;

        /* Interface button clicks */
        case 155:
            server_handle_if_button(player, buf);
            break;
        
        /* Item and object interaction packets */
        case 175: case 31: case 59: case 212:
        case 6:   case 159: case 231: case 235: case 237:
            buffer_skip(buf, packet_length);
            break;

        /* Interface interaction packets */
        case 194: case 8: case 27: case 113: case 100:
        case 202: case 134:
            buffer_skip(buf, packet_length);
            break;

        /* Command packets (::tele, ::item, etc.) */
        case 158:
            server_handle_command(player, buf, packet_length);
            break;
        
        /* Miscellaneous unhandled packets */
        case 189: case 190: case 4:
            buffer_skip(buf, packet_length);
            break;

        default:
            printf("Unhandled packet: opcode=%u, length=%u\n", opcode, packet_length);
            buffer_skip(buf, packet_length);
            break;
    }
}

/*
 * server_handle_movement_packet - Parse delta-encoded movement path
 * 
 * @param player         Player who sent the packet
 * @param buf            Packet payload buffer
 * @param packet_length  Payload size
 * 
 * PACKET STRUCTURE:
 *   [ctrl_down:1][start_x:2][start_z:2][dx0:1][dz0:1]...[dxN:1][dzN:1]
 *   
 *   ctrl_down: 1 = running, 0 = walking
 *   start_x:   Starting X coordinate (big-endian 16-bit)
 *   start_z:   Starting Z coordinate (big-endian 16-bit)
 *   dx, dz:    Signed 8-bit deltas (-128 to +127)
 * 
 * DELTA COUNT CALCULATION:
 *   packet_length = 1 + 2 + 2 + (count * 2)
 *   packet_length = 5 + (count * 2)
 *   count = (packet_length - 5) / 2
 *   
 *   Example: packet_length = 15
 *   count = (15 - 5) / 2 = 5 deltas
 * 
 * PATH RECONSTRUCTION:
 *   Start with absolute coordinates (start_x, start_z)
 *   Each delta adds relative offset to previous step
 *   
 *   steps[0] = (start_x, start_z)
 *   steps[1] = (start_x + dx0, start_z + dz0)
 *   steps[2] = (steps[1].x + dx1, steps[1].z + dz1)
 *   ...
 * 
 * DELTA ENCODING EXAMPLE:
 * 
 *   Player clicks from (3200, 3200) to (3205, 3203)
 *   Shortest path: East 5, North 3
 *   
 *   Absolute encoding: 6 steps * 4 bytes = 24 bytes
 *     (3200,3200), (3201,3200), (3202,3200), (3203,3200),
 *     (3204,3201), (3205,3202), (3205,3203)
 *   
 *   Delta encoding: 5 bytes header + 6 steps * 2 bytes = 17 bytes
 *     start: (3200, 3200)
 *     deltas: (+1,0), (+1,0), (+1,0), (+1,+1), (+1,+1), (0,+1)
 *   
 *   Savings: (24 - 17) / 24 = 29% bandwidth reduction
 * 
 * CURRENT POSITION HANDLING:
 *   If first step matches player's current position, skip it
 *   This prevents stationary "movement" when player clicks current tile
 * 
 * SIGNED DELTA PARSING:
 *   buffer_read_byte(buf, true) returns signed i8
 *   Must cast to signed before arithmetic to handle negative correctly
 *   
 *   Example:
 *     delta_byte = 0xFF (unsigned = 255)
 *     (i8)delta_byte = -1 (signed)
 *     3200 + (-1) = 3199 (correct)
 *     3200 + 255 = 3455 (wrong!)
 * 
 * COMPLEXITY: O(N) where N = number of steps (typically 5-25)
 */
static void server_handle_movement_packet(Player* player, StreamBuffer* buf, u32 packet_length, u8 opcode) {
    /* Read movement header */
    u32 ctrl_down = buffer_read_byte(buf, false);
    u32 start_x = buffer_read_short(buf, false, BYTE_ORDER_BIG);
    u32 start_z = buffer_read_short(buf, false, BYTE_ORDER_BIG);
    
    /* Calculate number of delta steps (minimap clicks have 14-byte offset at end) */
    u32 offset = (opcode == 165) ? 14 : 0;  /* 165 = MOVE_MINIMAPCLICK */
    u32 count = (packet_length - 5 - offset) / 2;
    
    /* Validate distance (TypeScript uses max 104 tiles from player) */
    i32 dx = (i32)start_x - (i32)player->position.x;
    i32 dz = (i32)start_z - (i32)player->position.z;
    i32 distance = (dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz);  /* Manhattan distance */
    
    printf("DEBUG: Movement packet received:\n");
    printf("  Opcode: %u (%s)\n", opcode, 
           opcode == 165 ? "MINIMAP" : opcode == 181 ? "VIEWPORT" : opcode == 93 ? "OPCLICK" : "UNKNOWN");
    printf("  Player position: (%u, %u)\n", player->position.x, player->position.z);
    printf("  Clicked destination: (%u, %u)\n", start_x, start_z);
    printf("  Delta: dx=%d, dz=%d\n", dx, dz);
    printf("  Manhattan distance: %d tiles\n", distance);
    printf("  Control held: %u (0=walk, 1=run)\n", ctrl_down);
    printf("  Delta waypoints: %u\n", count);
    
    if (distance > 104) {
        printf("WARNING: Movement rejected - distance exceeds max 104 tiles\n");
        return;
    }
    
    /* Allocate temporary array for reconstructed steps */
    typedef struct { u32 x; u32 z; } Step;
    Step steps[MAX_WAYPOINTS];
    u32 step_count = 0;
    
    /* First step is starting position (absolute coordinates) */
    steps[step_count].x = start_x;
    steps[step_count].z = start_z;
    step_count++;
    
    /* Read delta steps and reconstruct absolute coordinates */
    for (u32 i = 0; i < count && step_count < MAX_WAYPOINTS; i++) {
        i8 delta_x = (i8)buffer_read_byte(buf, true);  /* Signed 8-bit */
        i8 delta_z = (i8)buffer_read_byte(buf, true);
        steps[step_count].x = steps[step_count - 1].x + delta_x;
        steps[step_count].z = steps[step_count - 1].z + delta_z;
        step_count++;
    }
    
    printf("DEBUG: Player current pos=(%u,%u), path has %u steps\n", 
           player->position.x, player->position.z, step_count);
    
    /* Reset movement queue and configure run mode */
    movement_reset(&player->movement);
    movement_set_run_path(&player->movement, ctrl_down == 1);
    
    /* Skip first step if it matches current position */
    i32 start_idx = 0;
    if (step_count > 0 && steps[0].x == player->position.x && steps[0].z == player->position.z) {
        start_idx = 1;
        printf("DEBUG: Skipping first step as it's current position\n");
    }
    
    /* If client sent only destination (no intermediate deltas), calculate path */
    if (count == 0 && step_count == 1) {
        printf("DEBUG: Client sent destination only, calculating naive path\n");
        movement_naive_path(&player->movement, player->position.x, player->position.z, 
                           steps[0].x, steps[0].z);
    } else {
        /* Client sent full path, use it directly */
        for (i32 i = start_idx; i < step_count; i++) {
            movement_add_step(&player->movement, steps[i].x, steps[i].z);
            if (i == start_idx || i == step_count - 1) {
                printf("DEBUG: Adding step[%d]=(%u,%u)\n", i, steps[i].x, steps[i].z);
            }
        }
    }
    
    /* Finalize movement (prepares for tick processing) */
    movement_finish(&player->movement);
}

/*
 * server_handle_command - Process player-typed command
 * 
 * @param player         Player who sent the command
 * @param buf            Command string buffer
 * @param packet_length  String length
 * 
 * COMMAND FORMAT:
 *   Commands start with "::" prefix (e.g., "::tele 3200 3200 0")
 *   Sent as VAR_BYTE packet (opcode 158)
 *   Payload is raw ASCII string (not null-terminated!)
 * 
 * PARSING:
 *   Read packet_length bytes into char array
 *   Null-terminate manually (buffer_read_byte doesn't do this)
 *   Use sscanf() or strcmp() to parse command and arguments
 * 
 * IMPLEMENTED COMMANDS:
 * 
 *   ::tele <x> <z> <height>
 *     Teleport player to coordinates
 *     Example: ::tele 3200 3200 0
 *     Sends map region update after teleport
 *   
 *   Future commands:
 *     ::item <id> <amount>  - Spawn item
 *     ::npc <id>            - Spawn NPC
 *     ::god                 - Toggle invincibility
 * 
 * ARGUMENT PARSING:
 *   sscanf(args, "%u %u %u", &x, &z, &height)
 *     Returns number of successfully parsed arguments
 *     Check return value to validate input
 *     
 *   Example:
 *     Input: "3200 3200 0"
 *     Return: 3 (success)
 *     x=3200, z=3200, height=0
 *     
 *     Input: "3200 abc 0"
 *     Return: 1 (failure, only x parsed)
 * 
 * REGION UPDATE:
 *   After teleport, must send new map region:
 *     region_x = position_get_region_x(&player->position)
 *     region_y = position_get_region_z(&player->position)
 *     map_send_load_area(player, region_x, region_y)
 *   
 *   This loads terrain and objects for new area
 *   Without it, player sees black void or old map
 * 
 * COMPLEXITY: O(N) where N = command length (typically <50 chars)
 */
static void server_handle_command(Player* player, StreamBuffer* buf, u32 packet_length) {
    if (packet_length < 1) return;
    
    /* Debug: Print raw bytes (useful for protocol analysis) */
    printf("Command packet from %s, length=%u, raw bytes: ", player->username, packet_length);
    for (u32 i = 0; i < packet_length && i < 20; i++) {
        printf("%02X ", buf->data[buf->position + i]);
    }
    printf("\n");
    
    /* Read command string from buffer */
    char message[256];
    u32 pos = 0;
    
    while (pos < packet_length && pos < sizeof(message) - 1) {
        message[pos] = buffer_read_byte(buf, false);
        pos++;
    }
    message[pos] = '\0';  /* Null-terminate string */
    
    printf("Command from %s: '%s'\n", player->username, message);
    
    /* Parse and execute teleport command */
    if (strncmp(message, "::tele ", 7) == 0 || strncmp(message, "tele ", 5) == 0) {
        /* Find start of arguments (after "tele ") */
        const char* args = strstr(message, "tele ") + 5;
        u32 x = 0, z = 0, height = 0;
        
        /* Parse three unsigned integers */
        if (sscanf(args, "%u %u %u", &x, &z, &height) == 3) {
            printf("Teleporting %s to (%u, %u, %u)\n", player->username, x, z, height);
            
            /* Update player position */
            player_set_position(player, x, z, height);
            
            /* Send new map region to client */
            i32 mapsquare_x = position_get_mapsquare_x(&player->position);
            i32 mapsquare_z = position_get_mapsquare_z(&player->position);
            map_send_load_area(player, mapsquare_x, mapsquare_z);
        } else {
            /* Invalid arguments - send usage message */
            send_player_message(player, "Usage: ::tele <x> <z> <height>");
        }
    }
}

/*
 * server_handle_player_design - Process appearance customization
 * 
 * @param player  Player who sent the design
 * @param buf     Design data buffer
 * 
 * PACKET STRUCTURE:
 *   [gender:1][identikit0:1]...[identikit6:1][color0:1]...[color4:1]
 *   
 *   gender:       0 = male, 1 = female
 *   identikits:   Body part styles (hair, torso, legs, etc.)
 *   colors:       RGB indices for clothing colors
 * 
 * IDENTIKIT INDICES:
 *   0: Hair style
 *   1: Facial hair (beard)
 *   2: Torso (shirt)
 *   3: Arms
 *   4: Hands
 *   5: Legs (pants)
 *   6: Feet (boots)
 * 
 * COLOR INDICES:
 *   0: Hair color
 *   1: Torso color
 *   2: Leg color
 *   3: Feet color
 *   4: Skin color
 * 
 * APPEARANCE UPDATE:
 *   After receiving design, must:
 *   1. Mark UPDATE_APPEARANCE flag (tells update packet to rebuild)
 *   2. Re-send map region (ensures synchronization)
 *   3. Open main game viewport (interface 3559)
 *   4. Send sidebar tabs (inventory, stats, etc.)
 * 
 * INTERFACE SYSTEM:
 *   RuneScape uses hierarchical interfaces:
 *   - Root interface (3559) is main game viewport
 *   - Sidebar tabs are children (inventory, stats, etc.)
 *   - Each tab has a slot number (0-13)
 * 
 * SIDEBAR TAB MAPPING:
 *   Slot  Interface  Purpose
 *   0     5855       Combat styles
 *   1     3917       Stats
 *   2     638        Quest list
 *   3     3213       Inventory
 *   4     1644       Equipment
 *   5     5608       Prayer
 *   6     1151       Magic spellbook
 *   8     5065       Friends list
 *   9     5715       Ignore list
 *   10    2449       Logout
 *   11    904        Settings
 *   12    147        Emotes
 *   13    962        Music
 * 
 * COMPLEXITY: O(1) time (fixed-size packet)
 */
static void server_handle_player_design(Player* player, StreamBuffer* buf) {
    i32 gender = buffer_read_byte(buf, true);
    i32 identikits[7];
    for (int i = 0; i < 7; i++) {
        identikits[i] = buffer_read_byte(buf, true);
    }
    i32 colors[5];
    for (int i = 0; i < 5; i++) {
        colors[i] = buffer_read_byte(buf, false);
    }

    printf("IF_PLAYERDESIGN: gender=%d idkit=[%d,%d,%d,%d,%d,%d,%d] colors=[%d,%d,%d,%d,%d]\n", 
           gender, identikits[0], identikits[1], identikits[2], identikits[3], identikits[4], identikits[5], identikits[6],
           colors[0], colors[1], colors[2], colors[3], colors[4]);
    
    if (!player->allow_design) {
        printf("WARNING: IF_PLAYERDESIGN rejected - allow_design is false\n");
        return;
    }
    
    player->gender = (u8)gender;
    for (int i = 0; i < 7; i++) {
        player->body[i] = (i8)identikits[i];
    }
    for (int i = 0; i < 5; i++) {
        player->colors[i] = (u8)colors[i];
    }
    
    player->design_complete = true;
    player->update_flags |= UPDATE_APPEARANCE;

    printf("Player design saved: gender=%d body=[%d,%d,%d,%d,%d,%d,%d] colors=[%d,%d,%d,%d,%d]\n",
           player->gender, player->body[0], player->body[1], player->body[2], player->body[3],
           player->body[4], player->body[5], player->body[6],
           player->colors[0], player->colors[1], player->colors[2], player->colors[3], player->colors[4]);
}

/*
 * server_handle_if_button - Handle interface button clicks
 * 
 * @param player  Player who clicked button
 * @param buf     Button data buffer
 * 
 * PACKET STRUCTURE (opcode 155, 2 bytes):
 *   [component_id:2]  Big-endian component ID
 * 
 * This handles clicks on interface buttons like the "Accept" button
 * on the character design screen.
 */
static void server_handle_if_button(Player* player, StreamBuffer* buf) {
    u16 component_id = buffer_read_short(buf, false, BYTE_ORDER_BIG);
    
    printf("IF_BUTTON: player='%s' component=%u design_complete=%d\n", 
           player->username, component_id, player->design_complete);
    
    /* Handle logout button (component 2458) */
    if (component_id == 2458) {
        printf("Logout button clicked by player '%s'\n", player->username);
        
        /* Save player data */
        if (player->username[0] != '\0') {
            printf("Saving player '%s' before logout...\n", player->username);
            if (!player_save(player)) {
                printf("WARNING: Failed to save player '%s'\n", player->username);
            }
        }
        
        /* Send logout packet to client (opcode 142) */
        send_logout(player);
        
        /* Disconnect player */
        printf("Player '%s' logged out via logout button\n", player->username);
        player_disconnect(player);
        
        return;
    }
    
    if (player->design_complete) {
        player->allow_design = false;
        player->update_flags |= UPDATE_APPEARANCE;
        
        send_if_close(player);
        send_interfaces(player);
        
        player_save(player);
        
        printf("Closed design interface - player now in game world\n");
    }
}

/*
 * server_send_initial_game_packets - Send initial state after login
 * 
 * @param player  Newly logged in player
 * 
 * INITIALIZATION SEQUENCE:
 *   1. Register player with world (adds to active player list)
 *   2. Send map region (terrain and objects for current area)
 *   3. Send player stats (skills, levels, experience)
 *   4. Send inventory contents
 *   5. Send equipment contents
 *   6. Send interface configurations
 *   7. Send welcome message
 * 
 * MAP REGION:
 *   Player's position determines which 104x104 region to load
 *   Region coordinates: region_x = x >> 6, region_y = z >> 6
 *   
 *   Example:
 *     Player at (3200, 3200)
 *     region_x = 3200 >> 6 = 50
 *     region_y = 3200 >> 6 = 50
 *     
 *     Region 50,50 covers coordinates:
 *       X: 50*64 to 51*64-1 = 3200 to 3263
 *       Z: 50*64 to 51*64-1 = 3200 to 3263
 * 
 * INTERFACE SETUP:
 *   Sends multiple "fixed interface" packets (opcode 167)
 *   Each packet sets one child interface in the hierarchy
 *   This configures the entire UI layout
 * 
 * SIDEBAR TABS:
 *   NOT sent during initial login (causes issues)
 *   Sent later after player design is accepted
 *   This is a quirk of RuneScape protocol
 * 
 * WELCOME MESSAGE:
 *   Shows server name and revision info
 *   Appears in chatbox
 * 
 * COMPLEXITY: O(1) time (fixed number of packets)
 */
static void server_send_initial_game_packets(Player* player) {
    printf("Sending initial game packets to player '%s'\n", player->username);

    /* Register player with world (adds to active list) */
    login_send_initial_packets(player);

    /* Send map region for current area */
    i32 mapsquare_x = position_get_mapsquare_x(&player->position);
    i32 mapsquare_z = position_get_mapsquare_z(&player->position);
    map_send_load_area(player, mapsquare_x, mapsquare_z);

    /* Send player state */
    send_player_stats(player);      /* Skills and levels */
    send_inventory(player);         /* Inventory items */
    send_equipment(player);         /* Equipped items */
    send_interfaces(player);        /* UI configuration */

    /* Only open character design for new players */
    if (!player->design_complete) {
        printf("New player '%s' - opening character design interface\n", player->username);
        send_if_opentop(player, 3559);
        player->allow_design = true;
    } else {
        printf("Existing player '%s' - entering game world\n", player->username);
        /* Game world is visible by default (no IF_OPENTOP needed) */
    }

    /* Send welcome message */
    send_player_message(player, "Welcome to RuneScape by JAGeX.");
    send_player_message(player, "Protocol #225 Written in C (May 2004).");

    printf("Initial game packets sent to '%s'\n", player->username);
}

/*******************************************************************************
 * HELPER FUNCTIONS
 ******************************************************************************/

/*
 * server_process_players - Legacy function (no-op)
 * 
 * This function is no longer used. Player processing is handled
 * by world_process() during server_tick().
 * 
 * Kept for API compatibility (may be removed in future).
 */
void server_process_players(GameServer* server) {
    /* Delegated to world_process() in server_tick() */
}

/*
 * server_get_player - Get player by index (bounds-checked)
 * 
 * @param server  Pointer to GameServer
 * @param index   Player slot index (0 to MAX_PLAYERS-1)
 * @return        Pointer to Player, or NULL if out of bounds
 * 
 * BOUNDS CHECKING:
 *   Prevents buffer overflow by rejecting index >= MAX_PLAYERS
 *   Always check return value before dereferencing!
 * 
 * USAGE:
 *   Player* p = server_get_player(server, player_index);
 *   if (p) {
 *       // Safe to use p->...
 *   }
 * 
 * COMPLEXITY: O(1)
 */
Player* server_get_player(GameServer* server, u32 index) {
    if (index >= MAX_PLAYERS) return NULL;
    return &server->players[index];
}

/*
 * server_find_free_slot - Find first available player slot
 * 
 * @param server  Pointer to GameServer
 * @return        Pointer to free Player, or NULL if server full
 * 
 * ALGORITHM:
 *   Linear search from index 0 to MAX_PLAYERS-1
 *   Return first slot where state == PLAYER_STATE_DISCONNECTED
 *   Return NULL if all slots occupied
 * 
 * BEST CASE:
 *   First slot is free -> O(1)
 * 
 * WORST CASE:
 *   Server is full, scan all MAX_PLAYERS slots -> O(N)
 * 
 * AVERAGE CASE:
 *   Server half full, scan MAX_PLAYERS/2 slots -> O(N/2) = O(N)
 * 
 * OPTIMIZATION:
 *   Maintain free-list for O(1) allocation:
 *   
 *   u32 free_slots[MAX_PLAYERS];  // Stack of free indices
 *   u32 free_count = MAX_PLAYERS; // Number of free slots
 *   
 *   Player* server_find_free_slot_optimized(GameServer* server) {
 *       if (free_count == 0) return NULL;
 *       u32 index = free_slots[--free_count];
 *       return &server->players[index];
 *   }
 *   
 *   void server_release_slot(GameServer* server, u32 index) {
 *       free_slots[free_count++] = index;
 *   }
 * 
 * COMPLEXITY: O(N) where N = MAX_PLAYERS
 */
Player* server_find_free_slot(GameServer* server) {
    for (u32 i = 0; i < MAX_PLAYERS; i++) {
        if (server->players[i].state == PLAYER_STATE_DISCONNECTED) {
            return &server->players[i];
        }
    }
    return NULL;  /* Server full */
}
