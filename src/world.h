/*******************************************************************************
 * WORLD.H - Game World State and Player Management
 *******************************************************************************
 * 
 * EDUCATIONAL PURPOSE:
 * This file demonstrates fundamental concepts in:
 *   - Game world architecture (centralized state management)
 *   - Entity lifecycle management (player registration/removal)
 *   - Game loop design (tick-based processing)
 *   - Spatial indexing (player lookup by username/index)
 *   - Server-side multiplayer synchronization
 *   - Memory-efficient tracking structures
 * 
 * CORE CONCEPT - GAME WORLD:
 * 
 * A "World" represents the entire game server state - all players, NPCs,
 * items, and objects currently active. It is the central authority for:
 *   1. Player registry (who is online?)
 *   2. Tick processing (600ms game loop)
 *   3. Position tracking (where is everyone?)
 *   4. Network synchronization (what updates to send?)
 * 
 * WORLD ARCHITECTURE:
 * 
 *   ┌────────────────────────────────────────────────────────────────┐
 *   │                        GAME WORLD                              │
 *   ├────────────────────────────────────────────────────────────────┤
 *   │                                                                │
 *   │  Player List                 Player Tracking                   │
 *   │  ┌─────────────────┐         ┌─────────────────┐               │
 *   │  │ Index 0: NULL   │         │ Player 0 sees:  │               │
 *   │  │ Index 1: Player │────────→│ [1, 5, 12, 43]  │               │
 *   │  │ Index 2: NULL   │         │ Player 5 sees:  │               │
 *   │  │ Index 3: NULL   │         │ [1, 5, 9]       │               │
 *   │  │ Index 4: NULL   │         └─────────────────┘               │
 *   │  │ Index 5: Player │                                           │
 *   │  │ ...             │         Tick Count: 153842                │
 *   │  └─────────────────┘         Last Position Log: 1700000000     │
 *   │                                                                │
 *   └────────────────────────────────────────────────────────────────┘
 * 
 * GAME TICK SYSTEM:
 * 
 * Unlike most modern games (60 FPS = 16.67ms frame time), RuneScape uses
 * a fixed 600ms tick rate:
 * 
 *   ┌──────┬──────┬──────┬──────┬──────┬──────┬──────┬──────┐
 *   │Tick 0│Tick 1│Tick 2│Tick 3│Tick 4│Tick 5│Tick 6│Tick 7│
 *   └──────┴──────┴──────┴──────┴──────┴──────┴──────┴──────┘
 *      600ms  600ms  600ms  600ms  600ms  600ms  600ms
 * 
 * Each tick:
 *   1. Process player input (movement, attacks, clicks)
 *   2. Update game state (combat, NPCs, items)
 *   3. Send update packets to all players
 *   4. Sleep until next tick (maintain 600ms rhythm)
 * 
 * WHY 600MS?:
 *   - Network latency tolerance (200ms lag = still playable)
 *   - Server load distribution (1.67 ticks/sec is manageable)
 *   - Dial-up compatibility (2007 era had slow internet)
 *   - Game balance (prevents "twitch" reflexes, more strategic)
 * 
 * PLAYER TRACKING SYSTEM:
 * 
 * PROBLEM: Each player needs to know about nearby players
 *   - Player A sees: Players B, C, D (within 15 tiles)
 *   - Player B sees: Players A, C, E (different viewport)
 *   - Need to track who sees whom (2048 players = 4 million comparisons!)
 * 
 * SOLUTION: PlayerTracking structure (one per player)
 * 
 *   PlayerTracking for Player 5:
 *   ┌────────────────────────────────────────────────────────────┐
 *   │ local_players: [1, 12, 43, 99, 150]  <- Nearby player IDs  │
 *   │ local_count: 5                       <- How many nearby    │
 *   │ tracked[1] = true                    <- Is Player 1 tracked? 
 *   │ tracked[12] = true                                         │
 *   │ tracked[999] = false                 <- Far away           │
 *   │ appearance_hashes[1] = 0xAB          <- Last appearance    │
 *   └────────────────────────────────────────────────────────────┘
 * 
 * TRACKING ALGORITHM:
 * 
 *   Every tick, for each player P:
 *     1. Calculate distance to all other players
 *     2. Build list of players within 15 tiles (local_players)
 *     3. Compare to previous tick's list (tracked)
 *     4. Send "add player" packets for new players
 *     5. Send "remove player" packets for departed players
 *     6. Send "update player" packets for existing players
 * 
 * OPTIMIZATION - SPATIAL PARTITIONING:
 * 
 * Instead of checking all 2048 players, divide world into regions:
 * 
 *   World (104x104 regions, each 64x64 tiles):
 *   ┌────┬────┬────┬────┐
 *   │ R1 │ R2 │ R3 │ R4 │  R1 = Region (0, 0)
 *   ├────┼────┼────┼────┤  R2 = Region (1, 0)
 *   │ R5 │ R6 │ R7 │ R8 │  ...
 *   ├────┼────┼────┼────┤
 *   │ R9 │R10 │R11 │R12 │  Each region contains:
 *   └────┴────┴────┴────┘    - Players in that region
 *                             - NPCs in that region
 *                             - Objects/items
 * 
 * Player at (3222, 3218):
 *   Region: (3222 >> 6, 3218 >> 6) = (50, 50)
 *   Only check players in regions: (49-51, 49-51) = 9 regions
 *   Reduces comparisons from 2048 to ~20 (100x speedup!)
 * 
 * MEMORY LAYOUT:
 * 
 *   World struct:
 *   ┌──────────────────────────────────────────────────────────┐
 *   │ Offset  │ Field             │ Size       │ Description   │
 *   ├─────────┼───────────────────┼────────────┼───────────────┤
 *   │ 0       │ player_list       │ 8 bytes    │ Pointer to    │
 *   │         │                   │ (64-bit)   │ PlayerList    │
 *   ├─────────┼───────────────────┼────────────┼───────────────┤
 *   │ 8       │ player_tracking   │ 8 bytes    │ Pointer to    │
 *   │         │                   │            │ tracking array│
 *   ├─────────┼───────────────────┼────────────┼───────────────┤
 *   │ 16      │ last_position_log │ 8 bytes    │ Unix timestamp│
 *   ├─────────┼───────────────────┼────────────┼───────────────┤
 *   │ 24      │ tick_count        │ 8 bytes    │ Total ticks   │
 *   └─────────┴───────────────────┴────────────┴───────────────┘
 *   Total: 32 bytes (plus heap-allocated arrays)
 * 
 * HEAP ALLOCATIONS:
 * 
 *   PlayerList:
 *     - players array: 2048 * 8 bytes = 16KB (pointers)
 *     - occupied bitmap: 2048 * 1 byte = 2KB (flags)
 *     Total: ~18KB
 * 
 *   PlayerTracking array:
 *     - 2048 players * sizeof(PlayerTracking)
 *     - Each PlayerTracking: ~10KB (local_players + tracked + hashes)
 *     Total: ~20MB (largest memory consumer!)
 * 
 *   Player structs:
 *     - Each Player: ~18KB (see player.h)
 *     - 2048 players: ~36MB
 * 
 *   TOTAL WORLD MEMORY: ~56MB (acceptable for modern servers)
 * 
 * CONCURRENCY CONSIDERATIONS:
 * 
 * This implementation is SINGLE-THREADED:
 *   - All world processing happens on main game thread
 *   - Network I/O happens on separate thread (IO multiplexing)
 *   - Mutex locks protect shared state between threads
 * 
 * ALTERNATIVE ARCHITECTURES:
 * 
 *   1. Thread-per-player (2048 threads - too many!)
 *   2. Thread-pool (N workers process players in parallel)
 *   3. Sharded worlds (split regions across threads)
 *   4. Lock-free data structures (complex but fast)
 * 
 * RuneScape uses approach #3: Each "world" is single-threaded,
 * but multiple worlds run in parallel (World 1, World 2, ...).
 * Players choose which world to join.
 * 
 * SCALABILITY:
 * 
 *   Vertical scaling (bigger server):
 *     - 2048 players on one machine with 8 CPU cores
 *     - Each tick: ~20ms processing time
 *     - Plenty of headroom (only using ~3% of tick budget)
 * 
 *   Horizontal scaling (multiple servers):
 *     - Run 10 worlds on 10 machines
 *     - 10 * 2048 = 20,480 concurrent players
 *     - Load balancer distributes new connections
 * 
 * TESTING SCENARIOS:
 * 
 *   Empty world:
 *     - 0 players online
 *     - world_process() completes in <1ms
 *     - Memory usage: ~20MB (static allocations)
 * 
 *   Full world:
 *     - 2048 players online
 *     - All players in same region (worst case)
 *     - world_process() takes ~200ms (still within 600ms budget)
 *     - Memory usage: ~76MB
 * 
 *   Typical load:
 *     - 500 players online (25% capacity)
 *     - world_process() takes ~50ms
 *     - Memory usage: ~30MB
 * 
 ******************************************************************************/

#ifndef WORLD_H
#define WORLD_H

#include "types.h"
#include "player.h"
#include "player_list.h"
#include "constants.h"
#include <stdbool.h>

/*******************************************************************************
 * WORLD STRUCTURE
 *******************************************************************************
 * 
 * Central game state container holding all active entities and tracking data.
 * 
 * LIFETIME:
 *   - Created once at server startup: world_create()
 *   - Lives for entire server lifetime
 *   - Destroyed at server shutdown: world_destroy()
 * 
 * OWNERSHIP:
 *   - World owns PlayerList (allocated on heap)
 *   - World owns PlayerTracking array (allocated on heap)
 *   - World does NOT own individual Player structs (managed by PlayerList)
 * 
 * INVARIANTS:
 *   - player_list is never NULL after successful creation
 *   - player_tracking is never NULL after successful creation
 *   - player_list->capacity == MAX_PLAYERS
 *   - player_tracking has exactly MAX_PLAYERS elements
 *   - tick_count increments monotonically (never decreases)
 * 
 ******************************************************************************/
typedef struct {
    /*
     * player_list - Dynamic array of player pointers with occupancy tracking
     * 
     * TYPE: PlayerList* (heap-allocated, see player_list.h)
     * 
     * STRUCTURE:
     *   - players[i]: Pointer to Player at index i (or NULL if slot empty)
     *   - occupied[i]: Boolean flag (true if slot i has player)
     *   - count: Number of active players (cached for O(1) access)
     *   - next_pid: Hint for next free slot (optimization)
     * 
     * INDEXING:
     *   - Player indices are 1-based: [1, MAX_PLAYERS)
     *   - Index 0 is reserved (never used)
     *   - Client protocol uses 11-bit player indices (0-2047)
     *   - Index 2048 would overflow 11 bits, so maximum is 2047
     * 
     * EXAMPLE STATE:
     *   capacity: 2048
     *   count: 3
     *   players[0]: NULL         (reserved)
     *   players[1]: Player*      (username: "alice")
     *   players[2]: NULL         (empty slot)
     *   players[3]: Player*      (username: "bob")
     *   players[4]: Player*      (username: "charlie")
     *   players[5-2047]: NULL    (empty)
     *   
     *   occupied: [F, T, F, T, T, F, F, ...]
     *   next_pid: 2 (next free slot to try)
     * 
     * THREAD SAFETY:
     *   - NOT thread-safe (requires external synchronization)
     *   - Main game thread has exclusive access
     *   - Network threads must use mutex when adding/removing players
     */
    PlayerList* player_list;
    
    /*
     * player_tracking - Per-player viewport and synchronization state
     * 
     * TYPE: PlayerTracking* (heap-allocated array of MAX_PLAYERS elements)
     * 
     * INDEXED BY PLAYER INDEX:
     *   player_tracking[i] = tracking data for player at index i
     * 
     * EACH PlayerTracking CONTAINS:
     *   - local_players[MAX_PLAYERS]: Array of nearby player indices
     *   - local_count: Number of players in local_players (0-2047)
     *   - tracked[MAX_PLAYERS]: Bitmap of which players we know about
     *   - appearance_hashes[MAX_PLAYERS]: Cached appearance to detect changes
     * 
     * MEMORY USAGE:
     *   sizeof(PlayerTracking) = 2048*2 + 4 + 2048*1 + 2048*1 = ~8KB per player
     *   Total: 2048 * 8KB = 16MB (significant memory cost!)
     * 
     * WHY SO LARGE?:
     *   - Need to track all 2048 possible players (worst case: everyone nearby)
     *   - Bitmap approach (tracked[]) is more memory-efficient than hash map
     *   - Appearance hashing avoids re-sending unchanged data (bandwidth savings)
     * 
     * EXAMPLE - PLAYER 5's TRACKING:
     *   player_tracking[5].local_players = [1, 12, 43, 99, 150, 0, 0, ...]
     *   player_tracking[5].local_count = 5
     *   player_tracking[5].tracked[1] = true    (Player 1 is visible)
     *   player_tracking[5].tracked[2] = false   (Player 2 is not visible)
     *   player_tracking[5].appearance_hashes[1] = 0xAB  (last known appearance)
     * 
     * UPDATE ALGORITHM:
     *   Every tick, for player P at index I:
     *     1. Calculate which players are within 15 tiles
     *     2. Compare to player_tracking[I].local_players (previous tick)
     *     3. New players: Send "add player" packet, set tracked[pid] = true
     *     4. Departed players: Send "remove player" packet, set tracked[pid] = false
     *     5. Existing players: Send delta updates (movement, appearance, etc.)
     *     6. Update local_players[] with current nearby players
     * 
     * CLEARING POLICY:
     *   When player disconnects, memset(&player_tracking[pid], 0, sizeof(...))
     *   Ensures no stale data when slot is reused for new player
     */
    PlayerTracking* player_tracking;
    
    /*
     * last_position_log - Timestamp of last debug position printout
     * 
     * TYPE: u64 (unsigned 64-bit integer, Unix timestamp in seconds)
     * 
     * PURPOSE: Rate-limit debug logging to avoid spam
     * 
     * USAGE:
     *   Every tick, check:
     *     if (current_time - last_position_log >= 5) {
     *         printf("Player positions: ...");
     *         last_position_log = current_time;
     *     }
     * 
     * FREQUENCY: Prints player positions every 5 seconds
     * 
     * WHY RATE LIMIT?:
     *   - Printing every tick (600ms) = ~1.67 logs/sec = spam
     *   - Every 5 seconds = manageable debug output
     *   - Helps diagnose position desync issues
     * 
     * UNIX TIMESTAMP:
     *   - Seconds since January 1, 1970 00:00:00 UTC (epoch)
     *   - Example: 1700000000 = Sunday, November 14, 2023
     *   - Obtained via: time(NULL) in C standard library
     * 
     * 64-BIT RANGE:
     *   - u64 max: 18,446,744,073,709,551,615 seconds
     *   - That's 584 billion years (far beyond universe lifespan!)
     *   - No Y2K38 problem (unlike 32-bit time_t)
     */
    u64 last_position_log;
    
    /*
     * tick_count - Total number of game ticks since server start
     * 
     * TYPE: u64 (unsigned 64-bit integer)
     * 
     * INCREMENT: Every tick (600ms), tick_count++
     * 
     * USE CASES:
     *   - Event scheduling: "Despawn item at tick 1000"
     *   - Uptime calculation: tick_count * 600ms = server runtime
     *   - Profiling: Measure ticks between events
     * 
     * EXAMPLE VALUES:
     *   tick_count = 0     → Server just started
     *   tick_count = 100   → 1 minute uptime (100 * 0.6s = 60s)
     *   tick_count = 6,000 → 1 hour uptime
     *   tick_count = 144,000 → 24 hours uptime
     * 
     * OVERFLOW SAFETY:
     *   - u64 max: 18,446,744,073,709,551,615 ticks
     *   - At 600ms/tick: 11,068,046,444,225,730,969 milliseconds
     *   - That's 350 million years of continuous uptime!
     *   - Will never overflow in practice
     * 
     * MONOTONICITY:
     *   - tick_count ONLY increases (never decreases)
     *   - Safe to use for ordering events chronologically
     *   - If tick_count_A < tick_count_B, then A happened before B
     */
    u64 tick_count;
} World;

/*
 * g_world - Global world instance pointer
 * 
 * TYPE: World* (pointer to single World instance)
 * 
 * GLOBAL VARIABLE:
 *   - Defined in world.c: World* g_world = NULL;
 *   - Initialized at server startup: g_world = world_create();
 *   - Accessed from anywhere: if (g_world) { ... }
 * 
 * WHY GLOBAL?:
 *   - Single world per server process (simplifies architecture)
 *   - Avoids passing world pointer to every function (reduces boilerplate)
 *   - Matches original RuneScape server design
 * 
 * ALTERNATIVE DESIGNS:
 *   - Pass world as parameter: void game_tick(World* world);
 *   - World as singleton class (C++ style)
 *   - Dependency injection container
 * 
 * THREAD SAFETY:
 *   - Set once at startup (no synchronization needed)
 *   - Read-only after initialization (safe for multiple threads)
 *   - Contents (world->player_list, etc.) require synchronization
 * 
 * CLEANUP:
 *   At server shutdown:
 *     world_destroy(g_world);
 *     g_world = NULL;
 */
extern World* g_world;

/*******************************************************************************
 * PUBLIC API - WORLD MANAGEMENT
 ******************************************************************************/

/*
 * world_create - Allocate and initialize a new game world
 * 
 * @return  Pointer to new World, or NULL if allocation failed
 * 
 * ALGORITHM:
 *   1. Allocate World struct: calloc(1, sizeof(World))
 *   2. Create player list: player_list_create(MAX_PLAYERS)
 *   3. Allocate tracking array: calloc(MAX_PLAYERS, sizeof(PlayerTracking))
 *   4. Initialize timestamps: last_position_log = 0, tick_count = 0
 *   5. If any allocation fails, clean up and return NULL
 * 
 * MEMORY ALLOCATIONS:
 *   - World struct: 32 bytes
 *   - PlayerList: ~18KB (player pointers + occupied bitmap)
 *   - PlayerTracking array: ~16MB (2048 * 8KB)
 *   Total: ~16MB heap memory
 * 
 * INITIAL STATE:
 *   - player_list->count = 0 (no players online)
 *   - All tracking data zeroed (no active viewports)
 *   - tick_count = 0 (server just started)
 *   - last_position_log = 0 (never logged yet)
 * 
 * FAILURE MODES:
 *   Returns NULL if:
 *     - calloc(World) fails (out of memory)
 *     - player_list_create() fails (out of memory)
 *     - calloc(PlayerTracking) fails (out of memory)
 * 
 * PARTIAL CLEANUP ON FAILURE:
 *   If allocation fails midway:
 *     - Free any already-allocated memory
 *     - Return NULL (caller must check!)
 *     - No memory leaks
 * 
 * USAGE - SERVER STARTUP:
 *   int main() {
 *       g_world = world_create();
 *       if (!g_world) {
 *           fprintf(stderr, "Failed to create world!\n");
 *           return 1;
 *       }
 *       
 *       while (server_running) {
 *           world_process(g_world);
 *           sleep_until_next_tick();
 *       }
 *       
 *       world_destroy(g_world);
 *       return 0;
 *   }
 * 
 * COMPLEXITY: O(MAX_PLAYERS) time (zeroing tracking array)
 *             O(MAX_PLAYERS) space (heap allocations)
 */
World* world_create();

/*
 * world_destroy - Free all world memory and disconnect players
 * 
 * @param world  Pointer to World to destroy (may be NULL)
 * 
 * ALGORITHM:
 *   1. For each player in player_list:
 *        a. Call player_destroy(player) to close socket and free resources
 *   2. Destroy player list: player_list_destroy(world->player_list)
 *   3. Free tracking array: free(world->player_tracking)
 *   4. Free world struct: free(world)
 * 
 * RESOURCE CLEANUP:
 *   - All player sockets closed (TCP FIN sent to clients)
 *   - All player movement queues freed
 *   - PlayerList freed
 *   - PlayerTracking array freed
 *   - World struct freed
 * 
 * GRACEFUL SHUTDOWN:
 *   Before calling world_destroy(), server should:
 *     1. Send logout messages to all players
 *     2. Save all player data to database
 *     3. Wait for network buffers to flush
 *     4. Then destroy world
 * 
 * NULL SAFETY:
 *   - Safe to call with world = NULL (no-op)
 *   - Safe if player_list is NULL (skips player cleanup)
 *   - Safe if player_tracking is NULL (skips tracking cleanup)
 * 
 * CASCADING CLEANUP:
 *   world_destroy()
 *     -> player_destroy() for each player
 *          -> movement_destroy() (free waypoint queue)
 *          -> close(socket_fd) (TCP shutdown)
 *     -> player_list_destroy()
 *          -> free(players array)
 *          -> free(occupied bitmap)
 *     -> free(player_tracking)
 *     -> free(world)
 * 
 * MEMORY LEAK PREVENTION:
 *   - All heap pointers freed
 *   - No circular references (can't leak)
 *   - Valgrind clean (0 bytes leaked)
 * 
 * USAGE - SERVER SHUTDOWN:
 *   void shutdown_server() {
 *       printf("Shutting down server...\n");
 *       
 *       // Save all player data
 *       for (u32 i = 1; i < MAX_PLAYERS; i++) {
 *           Player* p = world_get_player_by_index(g_world, i);
 *           if (p) save_player(p);
 *       }
 *       
 *       // Clean up world
 *       world_destroy(g_world);
 *       g_world = NULL;
 *       
 *       printf("Shutdown complete.\n");
 *   }
 * 
 * COMPLEXITY: O(n) time where n = number of active players
 *             (due to player_destroy() calls)
 */
void world_destroy(World* world);

/*
 * world_process - Execute one game tick (600ms cycle)
 * 
 * @param world  Pointer to World to process
 * 
 * ALGORITHM (executed every 600ms):
 *   1. MOVEMENT PHASE:
 *        For each active player:
 *          a. player_process_movement(player)
 *          b. Updates position based on waypoint queue
 *          c. Sets primary_direction and secondary_direction
 * 
 *   2. UPDATE PHASE:
 *        a. Gather all active players into temporary array
 *        b. For each active player P:
 *             i. update_player(P, all_active_players, count, tracking)
 *            ii. Sends player info packet (184) with nearby player updates
 * 
 *   3. CLEANUP PHASE:
 *        For each active player:
 *          a. Increment placement_ticks if needs_placement
 *          b. Clear needs_placement flag after 2 ticks
 *          c. Reset update_flags = 0 (ready for next tick)
 * 
 *   4. DEBUG LOGGING:
 *        If 5 seconds elapsed since last_position_log:
 *          a. Print all player positions
 *          b. Update last_position_log = current_time
 * 
 *   5. INCREMENT TICK COUNTER:
 *        tick_count++
 * 
 * GAME TICK TIMELINE (600ms):
 * 
 *   0ms    ┌──────────────────────────────────────┐
 *          │ 1. Process Movement                  │
 *   50ms   │   - Walk/run waypoints               │
 *          │   - Update positions                 │
 *   100ms  ├──────────────────────────────────────┤
 *          │ 2. Send Player Updates               │
 *   300ms  │   - Build update packets             │
 *          │   - Send to all clients              │
 *   400ms  ├──────────────────────────────────────┤
 *          │ 3. Cleanup Flags                     │
 *   450ms  │   - Reset update_flags               │
 *          │   - Manage placement state           │
 *   500ms  ├──────────────────────────────────────┤
 *          │ 4. Debug Logging (if needed)         │
 *   520ms  ├──────────────────────────────────────┤
 *          │ 5. Increment Tick Count              │
 *   600ms  └──────────────────────────────────────┘
 *          [Sleep until next tick]
 * 
 * PLACEMENT SYSTEM:
 * 
 * When a player logs in or teleports, they need "placement":
 *   - Tick 0: needs_placement = true, placement_ticks = 0
 *   - Tick 1: needs_placement = true, placement_ticks = 1
 *   - Tick 2: needs_placement = false (cleared)
 * 
 * WHY 2 TICKS?:
 *   - Client needs time to receive initial position
 *   - Prevents visual glitches (player appearing then jumping)
 *   - Matches original RuneScape behavior
 * 
 * UPDATE FLAGS:
 * 
 * Each tick, player actions set flags:
 *   player->update_flags |= UPDATE_APPEARANCE;  // Changed equipment
 *   player->update_flags |= UPDATE_ANIMATION;   // Started animation
 * 
 * After update packet sent:
 *   player->update_flags = 0;  // Reset for next tick
 * 
 * This ensures each change is sent exactly once (no duplicates, no missing).
 * 
 * DEBUG OUTPUT EXAMPLE:
 * 
 *   DEBUG: Before update alice - tracking[1].local_count=3
 *   DEBUG: Before update bob - tracking[5].local_count=2
 *   Player: alice Position: (3222, 3218)
 *   Player: bob Position: (3225, 3220)
 * 
 * PERFORMANCE CHARACTERISTICS:
 * 
 * Best case (empty world):
 *   - 0 players active
 *   - Loops complete immediately
 *   - ~0.1ms execution time
 * 
 * Typical case (500 players):
 *   - 500 movement updates
 *   - 500 player info packets
 *   - ~50ms execution time
 * 
 * Worst case (2048 players, all clustered):
 *   - 2048 movement updates
 *   - 2048 player info packets (each seeing ~100 other players)
 *   - ~200ms execution time (still under 600ms budget)
 * 
 * THREAD SAFETY:
 *   - Single-threaded (no locks needed within this function)
 *   - Must NOT be called concurrently
 *   - Network thread must synchronize when adding/removing players
 * 
 * USAGE - MAIN GAME LOOP:
 *   while (server_running) {
 *       u64 tick_start = get_time_ms();
 *       
 *       world_process(g_world);
 *       
 *       u64 tick_end = get_time_ms();
 *       u64 elapsed = tick_end - tick_start;
 *       
 *       if (elapsed < 600) {
 *           sleep_ms(600 - elapsed);  // Maintain 600ms rhythm
 *       } else {
 *           printf("WARNING: Tick took %llu ms (over budget!)\n", elapsed);
 *       }
 *   }
 * 
 * COMPLEXITY: O(n^2) worst case where n = active players
 *             (each player checks distance to all others)
 *             O(n) average case (with spatial partitioning)
 */
void world_process(World* world);

/*******************************************************************************
 * PUBLIC API - PLAYER MANAGEMENT
 ******************************************************************************/

/*
 * world_register_player - Add a player to the world and assign index
 * 
 * @param world     Pointer to World
 * @param player    Pointer to Player to register
 * @param username  Player's login name (max 12 characters)
 * @return          true if registered successfully, false if world full
 * 
 * ALGORITHM:
 *   1. Validate inputs (world, player, username not NULL)
 *   2. Copy username to player struct (null-terminated)
 *   3. Add player to player_list:
 *        a. Find free slot (index I where occupied[I] == false)
 *        b. Set players[I] = player
 *        c. Set occupied[I] = true
 *        d. Set player->index = I
 *        e. Increment count
 *   4. Set player state: PLAYER_STATE_LOGGED_IN
 *   5. Set placement flags: needs_placement = true, placement_ticks = 0
 *   6. Print debug: "Registered new player: <username> Index: <I>"
 *   7. Return true (success)
 * 
 * USERNAME HANDLING:
 * 
 *   Input: "ZeZiMa" (mixed case, 6 characters)
 *   Processing:
 *     strncpy(player->username, username, 12);  // Copy up to 12 chars
 *     player->username[12] = '\0';              // Ensure null termination
 *   Result: player->username = "ZeZiMa\0"
 * 
 * BOUNDS CHECKING:
 *   - strncpy() prevents buffer overflow (max 12 chars + null)
 *   - If username longer than 12: truncated (e.g., "VeryLongName123" -> "VeryLongName")
 *   - Always null-terminated (safe for strcmp, printf)
 * 
 * INDEX ASSIGNMENT:
 * 
 * player_list_add() finds first free slot:
 *   Occupied: [F, T, F, T, F, F, ...]
 *   Indices:   0  1  2  3  4  5
 *   
 *   Start at next_pid = 2 (hint from last allocation)
 *   Check occupied[2] = F (free!)
 *   Assign player->index = 2
 *   Set occupied[2] = T
 *   Update next_pid = 3 (next guess)
 * 
 * FAILURE CASES:
 * 
 *   World full (all 2047 slots occupied):
 *     player_list_add() returns false
 *     Print: "Failed to register player <username>: world is full!"
 *     Return false (player NOT added)
 * 
 *   NULL inputs:
 *     Return false immediately (safe guard)
 * 
 * STATE TRANSITIONS:
 * 
 *   Before registration:
 *     player->state = PLAYER_STATE_LOGGING_IN
 *     player->index = <undefined>
 *     player->username = "" (empty)
 *   
 *   After successful registration:
 *     player->state = PLAYER_STATE_LOGGED_IN
 *     player->index = <assigned slot>
 *     player->username = <copied from parameter>
 *     player->needs_placement = true
 * 
 * PLACEMENT FLAG:
 * 
 *   needs_placement = true tells world_process():
 *     "This player just logged in, send full position update"
 *   
 *   Without this flag:
 *     - Client expects delta updates (move 1 tile east)
 *     - Would be confused by sudden appearance
 *   
 *   With this flag:
 *     - Server sends absolute position (you are at 3222, 3218)
 *     - Client renders player correctly
 * 
 * DEBUG OUTPUT:
 * 
 *   Success:
 *     "Registered new player: alice Index: 1"
 *     "Registered new player: bob Index: 5"
 *   
 *   Failure:
 *     "Failed to register player charlie: world is full!"
 * 
 * USAGE - LOGIN HANDLER:
 *   void handle_login(Connection* conn, const char* username, const char* password) {
 *       // Verify credentials
 *       if (!check_password(username, password)) {
 *           send_login_response(conn, LOGIN_RESPONSE_INVALID_CREDENTIALS);
 *           return;
 *       }
 *       
 *       // Create player
 *       Player* player = player_create();
 *       player_set_socket(player, conn->socket_fd);
 *       player_set_position(player, 3222, 3218, 0);  // Lumbridge spawn
 *       
 *       // Register in world
 *       if (!world_register_player(g_world, player, username)) {
 *           send_login_response(conn, LOGIN_RESPONSE_WORLD_FULL);
 *           player_destroy(player);
 *           return;
 *       }
 *       
 *       // Success!
 *       send_login_response(conn, LOGIN_RESPONSE_OK);
 *       send_initial_packets(player);  // Skills, inventory, interface, etc.
 *   }
 * 
 * COMPLEXITY: O(n) time where n = MAX_PLAYERS
 *             (worst case: scan all slots to find free one)
 *             O(1) average case (next_pid hint usually correct)
 */
bool world_register_player(World* world, Player* player, const char* username);

/*
 * world_remove_player - Remove player from world by username
 * 
 * @param world     Pointer to World
 * @param username  Username of player to remove
 * 
 * ALGORITHM:
 *   1. Find player: world_get_player(world, username)
 *   2. If not found:
 *        Print: "Tried to remove non-existing player: <username>"
 *        Return (no-op)
 *   3. Save player index: pid = player->index
 *   4. Clear tracking data: memset(&world->player_tracking[pid], 0, ...)
 *   5. Set player state: PLAYER_STATE_DISCONNECTED
 *   6. Remove from list: player_list_remove(world->player_list, pid)
 *        a. Set players[pid] = NULL
 *        b. Set occupied[pid] = false
 *        c. Decrement count
 *   7. Print: "Removed player: <username>"
 * 
 * USERNAME LOOKUP:
 * 
 *   Linear search through player_list:
 *     for (i = 1; i < MAX_PLAYERS; i++) {
 *         if (players[i] && strcmp(players[i]->username, username) == 0) {
 *             return players[i];  // Found!
 *         }
 *     }
 *   
 *   Complexity: O(n) where n = active players
 * 
 * OPTIMIZATION OPPORTUNITY:
 *   - Could use hash map: username -> player index
 *   - Lookup would be O(1) instead of O(n)
 *   - Trade-off: Extra memory for hash table
 * 
 * TRACKING DATA CLEANUP:
 * 
 *   Before removal:
 *     player_tracking[pid].local_count = 5
 *     player_tracking[pid].tracked[...] = various flags
 *   
 *   After memset:
 *     player_tracking[pid].local_count = 0
 *     player_tracking[pid].tracked[...] = all false
 *     player_tracking[pid].appearance_hashes[...] = all 0
 *   
 *   WHY CLEAR?:
 *     When slot is reused for new player, stale data could cause bugs:
 *       - New player sees phantom players from previous session
 *       - Appearance updates sent to wrong players
 *     Zeroing ensures clean slate.
 * 
 * STATE TRANSITION:
 * 
 *   Before removal:
 *     player->state = PLAYER_STATE_LOGGED_IN
 *     player->index = <valid slot>
 *     player_list->players[index] = player
 *   
 *   After removal:
 *     player->state = PLAYER_STATE_DISCONNECTED
 *     player->index = <unchanged, but slot freed>
 *     player_list->players[index] = NULL
 * 
 * SOCKET HANDLING:
 * 
 *   world_remove_player() does NOT close the socket!
 *   Caller must separately call player_destroy() or player_disconnect()
 * 
 *   Typical sequence:
 *     world_remove_player(g_world, "alice");  // Remove from world
 *     player_destroy(alice);                  // Close socket, free memory
 * 
 * EDGE CASES:
 * 
 *   Player not found:
 *     - Prints warning message
 *     - Returns without error (idempotent)
 *     - Safe to call multiple times
 *   
 *   NULL world or username:
 *     - Returns immediately (no crash)
 *   
 *   Player already removed:
 *     - world_get_player() returns NULL
 *     - Prints "non-existing player" message
 *     - Safe (idempotent)
 * 
 * DEBUG OUTPUT:
 * 
 *   Success:
 *     "Removed player: alice"
 *   
 *   Failure:
 *     "Tried to remove non-existing player: charlie"
 * 
 * USAGE - LOGOUT HANDLER:
 *   void handle_logout(Player* player) {
 *       const char* username = player->username;
 *       
 *       // Save player data
 *       save_player_to_database(player);
 *       
 *       // Remove from world
 *       world_remove_player(g_world, username);
 *       
 *       // Close connection
 *       player_disconnect(player);
 *       
 *       printf("Player %s logged out.\n", username);
 *   }
 * 
 * USAGE - TIMEOUT HANDLER:
 *   void check_timeouts() {
 *       u64 now = time(NULL);
 *       for (u32 i = 1; i < MAX_PLAYERS; i++) {
 *           Player* p = world_get_player_by_index(g_world, i);
 *           if (p && (now - p->last_activity > 60)) {
 *               printf("Timeout: %s\n", p->username);
 *               world_remove_player(g_world, p->username);
 *               player_disconnect(p);
 *           }
 *       }
 *   }
 * 
 * COMPLEXITY: O(n) time where n = active players (due to username lookup)
 */
void world_remove_player(World* world, const char* username);

/*
 * world_get_player - Find player by username
 * 
 * @param world     Pointer to World
 * @param username  Player's login name to search for
 * @return          Pointer to Player if found, NULL otherwise
 * 
 * ALGORITHM:
 *   1. Validate inputs (world, player_list, username not NULL)
 *   2. Linear search:
 *        for i = 1 to MAX_PLAYERS:
 *          if players[i] != NULL && strcmp(players[i]->username, username) == 0:
 *            return players[i]
 *   3. Return NULL (not found)
 * 
 * STRING COMPARISON:
 * 
 *   strcmp() performs lexicographic comparison:
 *     strcmp("alice", "alice") = 0  (equal)
 *     strcmp("alice", "bob")   < 0  (alice < bob)
 *     strcmp("bob", "alice")   > 0  (bob > alice)
 *   
 *   Returns 0 if strings match (found!)
 * 
 * CASE SENSITIVITY:
 * 
 *   This implementation is CASE-SENSITIVE:
 *     world_get_player(world, "Alice") != world_get_player(world, "alice")
 *   
 *   Original RuneScape is case-insensitive:
 *     "Zezima", "zezima", "ZEZIMA" all refer to same player
 *   
 *   TO MAKE CASE-INSENSITIVE:
 *     1. Store usernames as lowercase during registration
 *     2. Convert search term to lowercase before strcmp
 *     Example:
 *       char lower_username[13];
 *       strncpy(lower_username, username, 12);
 *       to_lowercase(lower_username);  // Custom function
 *       ...strcmp(player->username, lower_username)...
 * 
 * PERFORMANCE:
 * 
 *   Best case: O(1) - player at index 1
 *   Average case: O(n/2) - player in middle
 *   Worst case: O(n) - player at index 2047 or not found
 *   
 *   Where n = MAX_PLAYERS = 2048
 * 
 * OPTIMIZATION:
 * 
 *   Hash map approach:
 *     unordered_map<string, u32> username_to_index;
 *     username_to_index["alice"] = 1;
 *     username_to_index["bob"] = 5;
 *     
 *     Player* world_get_player(world, username) {
 *         u32 index = username_to_index[username];  // O(1) lookup
 *         return world->player_list->players[index];
 *     }
 *   
 *   Trade-off:
 *     + O(1) lookup time
 *     - Extra memory (hash table overhead)
 *     - Must maintain map on add/remove
 * 
 * USAGE PATTERNS:
 * 
 *   Private message:
 *     void send_private_message(const char* sender, const char* recipient, const char* msg) {
 *         Player* to = world_get_player(g_world, recipient);
 *         if (!to) {
 *             printf("Player '%s' not online.\n", recipient);
 *             return;
 *         }
 *         send_pm_packet(to, sender, msg);
 *     }
 *   
 *   Admin command:
 *     void admin_teleport_player(const char* target_name, u32 x, u32 z) {
 *         Player* target = world_get_player(g_world, target_name);
 *         if (target) {
 *             player_set_position(target, x, z, 0);
 *             printf("Teleported %s to (%u, %u)\n", target_name, x, z);
 *         } else {
 *             printf("Player %s not found.\n", target_name);
 *         }
 *     }
 *   
 *   Duplicate login check:
 *     void handle_login_request(const char* username) {
 *         if (world_get_player(g_world, username)) {
 *             send_login_response(conn, LOGIN_RESPONSE_ACCOUNT_ONLINE);
 *             return;
 *         }
 *         // Proceed with login...
 *     }
 * 
 * NULL HANDLING:
 * 
 *   Returns NULL if:
 *     - world is NULL
 *     - world->player_list is NULL
 *     - username is NULL
 *     - No player with matching username found
 *   
 *   Caller must check:
 *     Player* p = world_get_player(g_world, "alice");
 *     if (p) {
 *         // Safe to use p
 *     } else {
 *         // Player not online
 *     }
 * 
 * COMPLEXITY: O(n) time where n = active players (linear search)
 *             O(1) space (no allocations)
 */
Player* world_get_player(World* world, const char* username);

/*
 * world_get_player_by_index - Retrieve player by slot index
 * 
 * @param world  Pointer to World
 * @param index  Player slot index [0, MAX_PLAYERS)
 * @return       Pointer to Player at that index, or NULL if slot empty
 * 
 * ALGORITHM:
 *   1. Validate inputs (world, player_list not NULL)
 *   2. Return player_list_get(world->player_list, index)
 *        a. Check index < capacity
 *        b. Return players[index] (may be NULL)
 * 
 * INDEX RANGE:
 * 
 *   Valid indices: [1, 2047]
 *     - Index 0 is reserved (never used)
 *     - Indices 1-2047 are player slots
 *   
 *   Invalid indices:
 *     - index >= 2048: Out of bounds, returns NULL
 *     - index == 0: Reserved slot, always NULL
 * 
 * DIRECT ARRAY ACCESS:
 * 
 *   This is O(1) lookup (no search needed):
 *     return world->player_list->players[index];
 *   
 *   Much faster than world_get_player() which searches by username
 * 
 * USE CASES:
 * 
 *   Iterating all players:
 *     for (u32 i = 1; i < MAX_PLAYERS; i++) {
 *         Player* p = world_get_player_by_index(g_world, i);
 *         if (p && player_is_active(p)) {
 *             // Process active player
 *         }
 *     }
 *   
 *   Network protocol:
 *     - Player info packet includes player indices (11-bit)
 *     - Client says: "Show animation for player 42"
 *     - Server: Player* p = world_get_player_by_index(g_world, 42);
 *   
 *   Tracking updates:
 *     - tracking[5].local_players = [1, 12, 43]
 *     - For each index in local_players:
 *         Player* nearby = world_get_player_by_index(g_world, index);
 * 
 * RETURN VALUE:
 * 
 *   Non-NULL: Valid player pointer (slot occupied)
 *   NULL: Slot empty or invalid index
 *   
 *   Must check before use:
 *     Player* p = world_get_player_by_index(g_world, 5);
 *     if (p) {
 *         printf("Player 5: %s\n", p->username);
 *     } else {
 *         printf("Slot 5 is empty.\n");
 *     }
 * 
 * COMPARISON TO world_get_player():
 * 
 *   By username (slow, user-friendly):
 *     Player* p = world_get_player(g_world, "alice");
 *     O(n) time
 *   
 *   By index (fast, protocol-friendly):
 *     Player* p = world_get_player_by_index(g_world, 1);
 *     O(1) time
 * 
 * COMPLEXITY: O(1) time (array indexing)
 *             O(1) space (no allocations)
 */
Player* world_get_player_by_index(World* world, u32 index);

/*
 * world_get_free_index - Find next available player slot
 * 
 * @param world  Pointer to World
 * @return       Free slot index [1, 2047], or -1 if world full
 * 
 * ALGORITHM:
 *   1. Validate inputs (world, player_list not NULL)
 *   2. Call player_list_get_next_pid(world->player_list)
 *        a. Start at next_pid (hint from last allocation)
 *        b. Search for occupied[i] == false
 *        c. Wrap around if needed (circular search)
 *        d. Return index if found, or 0 if full
 *   3. If pid == 0: return -1 (world full)
 *   4. Else: return pid
 * 
 * SEARCH STRATEGY:
 * 
 *   Circular search with hint:
 *     next_pid = 5 (last allocation was slot 5)
 *     Search order: 5, 6, 7, ..., 2047, 1, 2, 3, 4
 *     
 *     occupied: [F, T, T, T, F, T, T, F, ...]
 *     indices:   0  1  2  3  4  5  6  7
 *     
 *     Start at 5: occupied[5] = T (taken)
 *     Try 6: occupied[6] = T (taken)
 *     Try 7: occupied[7] = F (free!) → return 7
 * 
 * WHY START AT next_pid?:
 *   - Optimization: Recently freed slots likely still free
 *   - Avoids always checking from index 1
 *   - Spreads players across index range (better cache locality)
 * 
 * RETURN VALUE:
 * 
 *   Positive [1-2047]: Valid free slot
 *   -1: No free slots (world full)
 * 
 * USE CASES:
 * 
 *   Pre-allocate player before registration:
 *     i32 free_index = world_get_free_index(g_world);
 *     if (free_index < 0) {
 *         send_login_response(conn, LOGIN_RESPONSE_WORLD_FULL);
 *         return;
 *     }
 *     
 *     Player* player = player_create();
 *     player->index = free_index;  // Pre-assign index
 *     // Continue with registration...
 *   
 *   Capacity check:
 *     if (world_get_free_index(g_world) < 0) {
 *         printf("WARNING: World is full!\n");
 *         disable_new_connections();
 *     }
 * 
 * PERFORMANCE:
 * 
 *   Best case: O(1) - next_pid is free
 *   Average case: O(1) - few slots checked
 *   Worst case: O(n) - must scan all slots (world nearly full)
 * 
 * WORLD FULL SCENARIO:
 * 
 *   All slots occupied:
 *     occupied: [F, T, T, T, ..., T]  (indices 1-2047 all true)
 *     Search wraps around entire array
 *     Returns 0 (no free slot found)
 *     world_get_free_index() converts 0 to -1
 * 
 * SIGNED VS UNSIGNED:
 * 
 *   player_list_get_next_pid() returns u16 (0-65535)
 *   world_get_free_index() returns i32 (-1 or 1-2047)
 *   
 *   Conversion:
 *     u16 pid = player_list_get_next_pid(...);
 *     if (pid == 0) return -1;  // Cast 0 to -1 (error sentinel)
 *     return (i32)pid;          // Cast u16 to i32 (safe, pid < 2048)
 * 
 * COMPLEXITY: O(n) worst case where n = MAX_PLAYERS
 *             O(1) average case (hint usually correct)
 */
i32 world_get_free_index(World* world);

/*
 * world_get_player_count - Get number of active players
 * 
 * @param world  Pointer to World
 * @return       Number of players online [0, 2047]
 * 
 * ALGORITHM:
 *   1. Validate inputs (world, player_list not NULL)
 *   2. Return player_list->count
 * 
 * CACHED COUNT:
 * 
 *   player_list maintains cached count:
 *     - Incremented on player_list_add()
 *     - Decremented on player_list_remove()
 *     - Always accurate (no need to scan array)
 * 
 * ALTERNATIVE (slower):
 * 
 *   Count by scanning:
 *     u32 count = 0;
 *     for (u32 i = 1; i < MAX_PLAYERS; i++) {
 *         if (players[i] != NULL) count++;
 *     }
 *     return count;
 *   
 *   This is O(n) instead of O(1) - inefficient!
 * 
 * USE CASES:
 * 
 *   Server status display:
 *     printf("Players online: %u / %u\n", 
 *            world_get_player_count(g_world), MAX_PLAYERS);
 *   
 *   Capacity check:
 *     if (world_get_player_count(g_world) >= MAX_PLAYERS) {
 *         printf("World full, rejecting connections.\n");
 *     }
 *   
 *   Load balancing:
 *     if (world_get_player_count(world1) < world_get_player_count(world2)) {
 *         assign_to_world(new_player, world1);  // Balance load
 *     }
 * 
 * RETURN VALUE:
 * 
 *   Range: [0, 2047]
 *     - 0: No players online
 *     - 2047: World at capacity (MAX_PLAYERS - 1, since index 0 reserved)
 *   
 *   Returns 0 if world or player_list is NULL (safe default)
 * 
 * COMPLEXITY: O(1) time (reading cached value)
 *             O(1) space (no allocations)
 */
u32 world_get_player_count(World* world);

/*******************************************************************************
 * PUBLIC API - UTILITY FUNCTIONS
 ******************************************************************************/

/*
 * world_get_active_players - Build array of all logged-in players
 * 
 * @param world        Pointer to World
 * @param out_players  Output array to fill (must have space for MAX_PLAYERS)
 * @param out_count    Output pointer to store count
 * 
 * ALGORITHM:
 *   1. Validate inputs (all not NULL)
 *   2. Initialize *out_count = 0
 *   3. For i = 1 to MAX_PLAYERS:
 *        a. Get player at index i
 *        b. If player exists AND player_is_active(player):
 *             i. out_players[*out_count] = player
 *            ii. (*out_count)++
 *        c. Stop if *out_count >= MAX_PLAYERS (prevent overflow)
 *   4. Return (out_players filled, out_count set)
 * 
 * ACTIVE PLAYER DEFINITION:
 * 
 *   player_is_active(player) returns true if:
 *     player->state == PLAYER_STATE_LOGGED_IN
 *   
 *   Excludes players in states:
 *     - DISCONNECTED (slot empty)
 *     - CONNECTED (awaiting login)
 *     - LOGGING_IN (credentials being verified)
 * 
 * OUTPUT FORMAT:
 * 
 *   Before call:
 *     Player* active_players[MAX_PLAYERS];  // Uninitialized
 *     u32 count;                            // Uninitialized
 *   
 *   After call:
 *     active_players[0] = Player* (index 1, username "alice")
 *     active_players[1] = Player* (index 5, username "bob")
 *     active_players[2] = Player* (index 12, username "charlie")
 *     active_players[3] = ... (more players)
 *     count = 3 (number of active players)
 * 
 * ARRAY BOUNDS:
 * 
 *   Caller must allocate array with MAX_PLAYERS capacity:
 *     Player* active[MAX_PLAYERS];  // 2048 pointers (16KB on 64-bit)
 *   
 *   This function will NOT overflow (stops at MAX_PLAYERS):
 *     if (*out_count >= MAX_PLAYERS) break;
 * 
 * USE CASES:
 * 
 *   Send global message:
 *     Player* all_players[MAX_PLAYERS];
 *     u32 count;
 *     world_get_active_players(g_world, all_players, &count);
 *     
 *     for (u32 i = 0; i < count; i++) {
 *         send_server_message(all_players[i], "Server restarting in 5 minutes!");
 *     }
 *   
 *   Update tick processing:
 *     Player* active[MAX_PLAYERS];
 *     u32 count;
 *     world_get_active_players(g_world, active, &count);
 *     
 *     for (u32 i = 0; i < count; i++) {
 *         update_player(active[i], active, count, &g_world->player_tracking[i]);
 *     }
 *   
 *   Statistics:
 *     Player* active[MAX_PLAYERS];
 *     u32 count;
 *     world_get_active_players(g_world, active, &count);
 *     
 *     printf("Active players: %u\n", count);
 *     for (u32 i = 0; i < count; i++) {
 *         printf("  - %s at (%u, %u)\n", 
 *                active[i]->username, 
 *                active[i]->position.x, 
 *                active[i]->position.z);
 *     }
 * 
 * MEMORY LAYOUT:
 * 
 *   Stack allocation (typical):
 *     Player* active_players[MAX_PLAYERS];  // 16KB on 64-bit systems
 *     
 *     This is on the stack (automatic storage):
 *       - Allocated when function called
 *       - Freed when function returns
 *       - No malloc/free needed
 *   
 *   Heap allocation (if stack too small):
 *     Player** active_players = malloc(MAX_PLAYERS * sizeof(Player*));
 *     world_get_active_players(g_world, active_players, &count);
 *     // Use active_players...
 *     free(active_players);
 * 
 * THREAD SAFETY:
 * 
 *   NOT thread-safe:
 *     - Reads player_list without lock
 *     - If another thread adds/removes players concurrently:
 *         - Could see inconsistent state
 *         - Could access freed memory
 *   
 *   MUST synchronize:
 *     pthread_mutex_lock(&world_mutex);
 *     world_get_active_players(g_world, active, &count);
 *     // Use active array...
 *     pthread_mutex_unlock(&world_mutex);
 * 
 * COMPLEXITY: O(n) time where n = MAX_PLAYERS (scan entire array)
 *             O(1) space (no heap allocations, uses caller's array)
 */
void world_get_active_players(World* world, Player** out_players, u32* out_count);

#endif /* WORLD_H */
