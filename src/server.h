/*******************************************************************************
 * SERVER.H - RuneScape Game Server Core Interface
 *******************************************************************************
 * 
 * This header defines the main game server architecture for a RuneScape
 * private server (revision 225, May 2004 protocol).
 * 
 * KEY COMPONENTS:
 *   1. GameServer structure (central server state)
 *   2. Game tick system (600ms tick rate)
 *   3. Player slot management (MAX_PLAYERS concurrent clients)
 *   4. Network event loop (non-blocking I/O)
 *   5. Lifecycle management (init/shutdown/run)
 * 
 * ARCHITECTURE OVERVIEW:
 * 
 *   ┌────────────────────────────────────────────────────────────┐
 *   │                      GAME SERVER                           │
 *   │  ┌──────────────────────────────────────────────────────┐  │
 *   │  │  Main Event Loop (server_run)                        │  │
 *   │  │  - Runs at maximum frequency (1ms sleep)             │  │
 *   │  │  - Dispatches to tick processor every 600ms          │  │
 *   │  └──────────────────────────────────────────────────────┘  │
 *   │                          |                                 │
 *   │        ┌─────────────────┴─────────────────┐               │
 *   │        |                                   |               │
 *   │  ┌─────▼─────┐                      ┌──────▼──────┐        │
 *   │  │  Accept   │                      │ Game Tick   │        │
 *   │  │   Loop    │                      │  Processor  │        │
 *   │  │           │                      │ (600ms)     │        │
 *   │  │ - Accept  │                      │             │        │
 *   │  │   clients │                      │ - World     │        │
 *   │  │ - Process │                      │   updates   │        │
 *   │  │   packets │                      │ - NPC       │        │
 *   │  └───────────┘                      │ - Combat    │        │
 *   │                                     └─────────────┘        │
 *   │  ┌──────────────────────────────────────────────────────┐  │
 *   │  │  Player Array [MAX_PLAYERS]                          │  │
 *   │  │  - Socket management                                 │  │
 *   │  │  - Movement processing                               │  │
 *   │  │  - Update flags                                      │  │
 *   │  └──────────────────────────────────────────────────────┘  │
 *   │  ┌──────────────────────────────────────────────────────┐  │
 *   │  │  Network Server (non-blocking TCP socket)            │  │
 *   │  │  - Listen socket (port 43594 default)                │  │
 *   │  │  - Connection queue                                  │  │
 *   │  └──────────────────────────────────────────────────────┘  │
 *   └────────────────────────────────────────────────────────────┘
 * 
 * GAME TICK SYSTEM:
 * 
 * RuneScape uses a fixed 600ms tick rate (approximately 1.67 ticks/second)
 * This is a fundamental design choice that affects all game mechanics:
 * 
 * Timeline (600ms per tick):
 * 
 *   Tick 0         Tick 1         Tick 2         Tick 3
 *   |------------->|------------->|------------->|
 *   0ms           600ms         1200ms         1800ms
 *   
 *   Player actions are queued and processed at tick boundaries
 *   Movement, combat, and updates happen synchronously per tick
 * 
 * Why 600ms?
 *   - Balance between responsiveness and server load
 *   - Network latency tolerance (players see updates 1-2 ticks delayed)
 *   - Simplifies game logic (discrete time steps vs continuous)
 *   - Historical precedent (original RuneScape design)
 * 
 * TICK PROCESSING ORDER:
 *   1. Process queued player actions (movement, combat, interactions)
 *   2. Update NPC and movement
 *   3. Process combat calculations
 *   4. Update world state (spawns, despawns, timers)
 *   5. Build and send player update packets
 *   6. Build and send NPC update packets
 *   7. Process respawns and cleanup
 * 
 * PLAYER SLOT MANAGEMENT:
 * 
 * The server maintains a fixed-size array of player slots (MAX_PLAYERS).
 * Each slot can be in one of several states:
 * 
 *   DISCONNECTED: Slot is free, available for new connections
 *   CONNECTED:    Socket connected, login in progress
 *   LOGGED_IN:    Player fully authenticated and in-game
 * 
 * Slot allocation diagram:
 * 
 *   Index: 0    1    2    3    4    5    ...  MAX_PLAYERS-1
 *        ┌────┬────┬────┬────┬────┬────┬────┬────┐
 *        │Free│Used│Used│Free│Used│Free│ ...│Free│
 *        └────┴────┴────┴────┴────┴────┴────┴────┘
 *             │    │         │
 *             │    │         └─> Player "Bob" (index 4)
 *             │    └───────────> Player "Alice" (index 2)
 *             └────────────────> Player "Charlie" (index 1)
 * 
 * SERVER LIFECYCLE:
 * 
 *   1. INITIALIZATION (server_init):
 *      - Allocate world state
 *      - Load cache files (maps, items, NPCs)
 *      - Initialize subsystems (items, NPCs, objects)
 *      - Create TCP listen socket
 *      - Zero-initialize player slots
 * 
 *   2. RUNNING (server_run):
 *      - Infinite loop until shutdown signal
 *      - Accept new connections
 *      - Process incoming packets
 *      - Execute game tick every 600ms
 *      - Sleep 1ms to prevent CPU spinning
 * 
 *   3. SHUTDOWN (server_shutdown):
 *      - Disconnect all players gracefully
 *      - Close network sockets
 *      - Free all subsystems (reverse init order)
 *      - Cleanup memory
 * 
 * PACKET PROCESSING FLOW:
 * 
 *   ┌─────────────────┐
 *   │ Client Socket   │
 *   └────────┬────────┘
 *            │ TCP recv()
 *            ▼
 *   ┌─────────────────┐
 *   │ Input Buffer    │ <- Accumulate bytes until full packet
 *   │ (per-player)    │
 *   └────────┬────────┘
 *            │ Parse header
 *            ▼
 *   ┌─────────────────┐
 *   │ ISAAC Decrypt   │ <- Decrypt opcode if cipher initialized
 *   │ (opcode only)   │
 *   └────────┬────────┘
 *            │
 *            ▼
 *   ┌─────────────────┐
 *   │ Packet Handler  │ <- Dispatch by opcode (switch statement)
 *   │ (server.c)      │
 *   └────────┬────────┘
 *            │
 *            ▼
 *   ┌─────────────────┐
 *   │ Game Logic      │ <- Movement, combat, interactions
 *   │ (various files) │
 *   └─────────────────┘
 * 
 * PERFORMANCE CONSIDERATIONS:
 * 
 * Tick Budget:
 *   - Must complete all processing within 600ms
 *   - Target: <300ms average (50% headroom)
 *   - Pathological cases: O(N^2) player visibility checks
 *   
 *   With MAX_PLAYERS = 2048:
 *     Worst case player updates: 2048 * 2048 = 4M operations
 *     Mitigation: Local player lists (only nearby players)
 *     Typical: 100-200 players visible = 20K-40K operations
 * 
 * Memory Usage:
 *   Each player slot: approximately 4KB (Player struct + buffers)
 *   2048 players = approximately 8MB baseline
 *   World state, NPCs, items add approximately 50MB
 *   Total server footprint: approximately 100-200MB
 * 
 * Network I/O:
 *   - Non-blocking sockets (prevent stalls on slow clients)
 *   - Per-player input buffering (handle partial packets)
 *   - Output buffering (batch multiple packets)
 *   - Peak bandwidth: approximately 50KB/s per player = 100MB/s for 2048 players
 * 
 * THREAD SAFETY:
 * 
 * Current implementation is SINGLE-THREADED:
 *   - Simplifies logic (no locks needed)
 *   - Easier debugging (deterministic execution)
 *   - Sufficient for 2000+ players on modern CPUs
 * 
 * Multi-threaded design considerations:
 *   - Worker threads for packet processing
 *   - Region-based world partitioning
 *   - Lock-free message passing
 *   - Not implemented yet (YAGNI principle)
 * 
 * EDUCATIONAL CONCEPTS:
 *   - Event-driven architecture
 *   - Fixed time-step game loop
 *   - Non-blocking I/O
 *   - Resource pooling (player slots)
 *   - Graceful degradation (reject connections when full)
 * 
 ******************************************************************************/

#ifndef SERVER_H
#define SERVER_H

#include "types.h"
#include "player.h"
#include "network.h"

/*
 * GameServer - Central server state structure
 * 
 * MEMORY LAYOUT:
 * ┌─────────────────────────────────────────────────────────────┐
 * │ GameServer struct (approximately 8MB for 2048 players)      │
 * ├─────────────────────────────────────────────────────────────┤
 * │ network:    NetworkServer (socket, port, etc.)              │
 * │ players:    Player[MAX_PLAYERS] (array of player slots)     │
 * │ running:    bool (server running flag)                      │
 * │ tick_count: u64 (total ticks since startup)                 │
 * └─────────────────────────────────────────────────────────────┘
 * 
 * FIELDS:
 * 
 * network (NetworkServer):
 *   - Listen socket for accepting connections
 *   - Port number (default 43594)
 *   - Socket options (non-blocking, reuse address)
 * 
 * players (Player[MAX_PLAYERS]):
 *   - Fixed array of player slots
 *   - Indexed 0 to MAX_PLAYERS-1
 *   - Each slot can be DISCONNECTED/CONNECTED/LOGGED_IN
 *   - Allows O(1) player lookup by index
 * 
 * running (bool):
 *   - Set to true during initialization
 *   - Checked every iteration of main loop
 *   - Set to false to trigger graceful shutdown
 * 
 * tick_count (u64):
 *   - Incremented every 600ms tick
 *   - Used for periodic events (e.g., every 100 ticks = 1 minute)
 *   - Wraps after 584 million years at 600ms tick rate
 * 
 * SIZE ANALYSIS:
 *   sizeof(NetworkServer)    approximately 64 bytes
 *   sizeof(Player) * 2048    approximately 8MB
 *   sizeof(bool)             1 byte
 *   sizeof(u64)              8 bytes
 *   Total:                   approximately 8MB + padding
 */
typedef struct GameServer {
    NetworkServer network;              /* TCP listen socket */
    Player players[MAX_PLAYERS];        /* Player slot array */
    bool running;                       /* Server running flag */
    u64 tick_count;                     /* Total ticks elapsed */
} GameServer;

/*
 * server_init - Initialize game server and all subsystems
 * 
 * @param server  Pointer to GameServer structure (caller-allocated)
 * @param port    TCP port to listen on (43594 for production)
 * @return        true on success, false on failure
 * 
 * INITIALIZATION SEQUENCE:
 * 
 *   1. Zero-initialize server structure (memset)
 *   2. Create world state (regions, tiles, collision)
 *   3. Initialize cache system (load definitions)
 *   4. Initialize item system (definitions, spawns)
 *   5. Initialize NPC system (spawns, AI)
 *   6. Initialize object system (scenery, interactive objects)
 *   7. Initialize player slots (set all to DISCONNECTED)
 *   8. Create TCP listen socket on specified port
 *   9. Set server->running = true
 * 
 * ERROR HANDLING:
 *   If any subsystem fails to initialize, cleanup is performed
 *   in reverse order and false is returned. Caller should not
 *   call server_run() after init failure.
 * 
 * EXAMPLE USAGE:
 *   GameServer server;
 *   if (!server_init(&server, 43594)) {
 *       fprintf(stderr, "Failed to start server\n");
 *       return 1;
 *   }
 *   server_run(&server);
 *   server_shutdown(&server);
 * 
 * COMPLEXITY: O(1) time (initialization is constant)
 */
bool server_init(GameServer* server, u16 port);

/*
 * server_shutdown - Cleanup and deallocate all server resources
 * 
 * @param server  Pointer to initialized GameServer
 * 
 * SHUTDOWN SEQUENCE:
 * 
 *   1. Set server->running = false (stop main loop)
 *   2. Disconnect all players (iterate player array)
 *      - Send logout packets
 *      - Close sockets
 *      - Save player data
 *   3. Shutdown network (close listen socket)
 *   4. Destroy object system
 *   5. Destroy NPC system
 *   6. Destroy item system
 *   7. Destroy cache
 *   8. Destroy world
 * 
 * Order is important: reverse of initialization to prevent
 * use-after-free bugs (e.g., must destroy NPCs before world).
 * 
 * THREAD SAFETY: Not thread-safe (must be called from main thread)
 * 
 * COMPLEXITY: O(N) where N = number of connected players
 */
void server_shutdown(GameServer* server);

/*
 * server_run - Main event loop (runs until shutdown)
 * 
 * @param server  Pointer to initialized GameServer
 * 
 * MAIN LOOP STRUCTURE:
 * 
 *   while (server->running) {
 *       // 1. Check if 600ms elapsed since last tick
 *       if (elapsed >= TICK_RATE_MS) {
 *           server_tick(server);  // Process game tick
 *       }
 *       
 *       // 2. Accept new connections (non-blocking)
 *       server_process_connections(server);
 *       
 *       // 3. Process incoming packets from all players
 *       server_process_packets(server);
 *       
 *       // 4. Sleep briefly to prevent CPU spinning
 *       usleep(1000);  // 1ms sleep
 *   }
 * 
 * TIMING DIAGRAM (one second of execution):
 * 
 *   Time:    0ms     100ms   200ms   300ms   400ms   500ms   600ms
 *   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤
 *   │        │       │       │       │       │       │       │
 *   │ Accept │Accept │Accept │Accept │Accept │Accept │TICK   │
 *   │ Packet │Packet │Packet │Packet │Packet │Packet │+ all  │
 *   │ Sleep  │Sleep  │Sleep  │Sleep  │Sleep  │Sleep  │others │
 *   │        │       │       │       │       │       │       │
 *   └────────┴───────┴───────┴───────┴───────┴───────┴───────┘
 *   
 *   Every 600ms: Full tick processing (player updates, NPC, etc.)
 *   Every 1ms:   Check for new connections and packets
 * 
 * WHY 1ms SLEEP?
 *   - Without sleep, loop would consume 100% CPU
 *   - 1ms balances responsiveness vs CPU usage
 *   - Allows OS to schedule other processes
 *   - Still checks 1000 times per second (very responsive)
 * 
 * TICK RATE PRECISION:
 *   Uses clock_gettime(CLOCK_MONOTONIC) for accurate timing
 *   Tick rate: 600ms +/- 1ms (0.16% error)
 *   Drift correction: Uses elapsed time, not fixed 600ms increments
 * 
 * COMPLEXITY: Infinite loop (O(infinity) in theory)
 */
void server_run(GameServer* server);

/*
 * server_tick - Process one game tick (600ms cycle)
 * 
 * @param server  Pointer to GameServer
 * 
 * TICK PROCESSING:
 *   1. Increment tick counter
 *   2. Process world state (world_process)
 *      - Movement updates
 *      - Player visibility calculations
 *      - NPC updates
 *      - Combat processing
 *      - Spawn/despawn timers
 *   3. Send update packets to all players
 * 
 * TICK BUDGET:
 *   Must complete within 600ms to maintain tick rate
 *   Target: <300ms for safety margin
 *   Monitoring: Should log slow ticks for optimization
 * 
 * COMPLEXITY: O(N * M) where:
 *   N = number of players
 *   M = average players visible to each player (typically 50-200)
 */
void server_tick(GameServer* server);

/*
 * server_process_connections - Accept pending connection (non-blocking)
 * 
 * @param server  Pointer to GameServer
 * 
 * ALGORITHM:
 *   1. Call network_accept_connection (non-blocking)
 *   2. If no pending connection, return immediately
 *   3. Find free player slot (server_find_free_slot)
 *   4. If server full, close socket and return
 *   5. Assign socket to player slot
 *   6. Begin login handshake (login_process_connection)
 * 
 * NON-BLOCKING BEHAVIOR:
 *   - Returns immediately if no connection pending
 *   - Never blocks main loop
 *   - Accepts at most one connection per call
 * 
 * CAPACITY HANDLING:
 *   When server is full (all slots occupied):
 *   - New connection is immediately closed
 *   - Client sees "Server is full" or connection reset
 *   - No data is sent (immediate FIN/RST)
 * 
 * COMPLEXITY: O(N) where N = MAX_PLAYERS (due to find_free_slot)
 *             Optimization possible with free-list
 */
void server_process_connections(GameServer* server);

/*
 * server_process_packets - Read and dispatch packets from all players
 * 
 * @param server  Pointer to GameServer
 * 
 * ALGORITHM:
 *   For each player slot:
 *     1. If socket invalid, skip
 *     2. Try to recv() data (non-blocking)
 *     3. Append to input buffer
 *     4. If player is logging in, process login packet
 *     5. If player is logged in, parse and dispatch game packets
 *     6. If recv() returns 0, player disconnected
 * 
 * PACKET PARSING:
 * 
 *   Input buffer: [opcode][len?][payload][opcode][len?][payload]...
 *                  ^------packet 1------^------packet 2------^
 *   
 *   1. Decrypt opcode using ISAAC cipher
 *   2. Lookup packet length in PacketLengths table:
 *      - Fixed length: predefined size (e.g., 0 = no payload)
 *      - VAR_BYTE (-1): next byte is payload length
 *      - VAR_SHORT (-2): next 2 bytes are payload length
 *   3. Check if full packet received (buffer size >= header + length)
 *   4. If incomplete, break and wait for more data
 *   5. Extract packet payload into StreamBuffer
 *   6. Dispatch to handler (server_handle_packet)
 *   7. Remove packet from input buffer (memmove remaining data)
 *   8. Repeat until buffer empty
 * 
 * PARTIAL PACKET HANDLING:
 * 
 *   Example: Receive 10 bytes, packet needs 15
 *   
 *   Iteration 1:
 *     in_buffer_size = 10
 *     packet_length = 15
 *     10 < 15, so break and wait
 *   
 *   Next call (5 more bytes received):
 *     in_buffer_size = 15
 *     packet_length = 15
 *     15 == 15, process packet!
 * 
 * COMPLEXITY: O(N * P) where:
 *   N = number of players
 *   P = packets per player (typically 1-10 per iteration)
 */
void server_process_packets(GameServer* server);

/*
 * server_process_players - Process all player logic
 * 
 * @param server  Pointer to GameServer
 * 
 * NOTE: Currently delegated to world_process() in server_tick()
 *       This function is a legacy interface and may be removed.
 * 
 * COMPLEXITY: O(1) (currently a no-op)
 */
void server_process_players(GameServer* server);

/*
 * server_get_player - Get player by index (bounds-checked)
 * 
 * @param server  Pointer to GameServer
 * @param index   Player slot index (0 to MAX_PLAYERS-1)
 * @return        Pointer to Player, or NULL if index out of bounds
 * 
 * BOUNDS CHECKING:
 *   Returns NULL if index >= MAX_PLAYERS
 *   Prevents buffer overflow attacks
 * 
 * USAGE:
 *   Player* p = server_get_player(server, 42);
 *   if (p && p->state == PLAYER_STATE_LOGGED_IN) {
 *       // Safe to use player
 *   }
 * 
 * COMPLEXITY: O(1)
 */
Player* server_get_player(GameServer* server, u32 index);

/*
 * server_find_free_slot - Find first available player slot
 * 
 * @param server  Pointer to GameServer
 * @return        Pointer to free Player slot, or NULL if server full
 * 
 * ALGORITHM:
 *   Linear search from index 0 to MAX_PLAYERS-1
 *   Return first slot where state == PLAYER_STATE_DISCONNECTED
 * 
 * OPTIMIZATION OPPORTUNITY:
 *   Could maintain a free-list for O(1) allocation:
 *   - Keep stack of free indices
 *   - Push on disconnect, pop on connect
 *   - Trade memory for speed (extra 2048 * 4 = 8KB)
 * 
 * COMPLEXITY: O(N) where N = MAX_PLAYERS
 *             Worst case: server is full, scans all 2048 slots
 *             Average case: O(1) when server is not full
 */
Player* server_find_free_slot(GameServer* server);

/*
 * g_server - Global server instance pointer
 * 
 * GLOBAL STATE RATIONALE:
 *   - Simplifies callback interfaces (no need to pass server everywhere)
 *   - Allows signal handlers to access server (for graceful shutdown)
 *   - Single server instance per process (RuneScape design)
 * 
 * DRAWBACKS:
 *   - Prevents running multiple servers in one process
 *   - Makes unit testing harder (global state)
 *   - Not thread-safe
 * 
 * USAGE:
 *   extern GameServer* g_server;  // Declare in other files
 *   
 *   void some_function() {
 *       if (g_server && g_server->running) {
 *           // Use global server
 *       }
 *   }
 */
extern GameServer* g_server;

#endif /* SERVER_H */
