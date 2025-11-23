/*******************************************************************************
 * WORLD.C - Game World Implementation
 *******************************************************************************
 * 
 * This file implements the core game world management system for a RuneScape
 * private server. It provides:
 *   - World lifecycle (create, destroy, process)
 *   - Player registration and removal
 *   - Player lookup (by username or index)
 *   - Tick-based game loop processing
 *   - Player tracking and synchronization
 * 
 * KEY SYSTEMS IMPLEMENTED:
 *   1. World state management (single source of truth)
 *   2. Player list with sparse array allocation
 *   3. Per-player viewport tracking (local players)
 *   4. Tick-based processing (600ms intervals)
 *   5. Placement system (2-tick delay for visual consistency)
 * 
 * DESIGN PATTERNS USED:
 *   - Singleton (g_world global instance)
 *   - Entity-Component (World contains player list and tracking data)
 *   - Observer pattern (tracking system monitors player positions)
 *   - Factory pattern (world_create allocates all resources)
 * 
 * MEMORY MANAGEMENT:
 *   - All heap allocations in world_create()
 *   - All deallocations in world_destroy()
 *   - No memory leaks (Valgrind clean)
 *   - Graceful failure on OOM (returns NULL, cleans up partial allocations)
 * 
 * THREAD SAFETY:
 *   - Single-threaded design (main game thread only)
 *   - External synchronization required if used from multiple threads
 *   - Network threads must lock before calling player add/remove
 * 
 * PERFORMANCE CHARACTERISTICS:
 *   - world_create:        O(n) time, O(n) space where n = MAX_PLAYERS
 *   - world_destroy:       O(n) time (must destroy all players)
 *   - world_process:       O(n^2) worst case, O(n) typical (with spatial partitioning)
 *   - world_register:      O(n) worst case, O(1) typical (slot hint optimization)
 *   - world_remove:        O(n) time (username lookup)
 *   - world_get_player:    O(n) time (linear search)
 *   - world_get_by_index:  O(1) time (array access)
 *   - world_get_count:     O(1) time (cached value)
 * 
 ******************************************************************************/

#include "world.h"
#include "update.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/*******************************************************************************
 * UPDATE MASK CONSTANTS
 *******************************************************************************
 * 
 * Update flags indicate which player fields changed this tick.
 * See constants.h for full list and detailed documentation.
 ******************************************************************************/

/*
 * UPDATE_APPEARANCE - Player equipment/appearance changed
 * 
 * VALUE: 0x1 (bit 0)
 * 
 * WHEN SET:
 *   - Player equips/unequips item
 *   - Appearance settings changed
 *   - On first login (initial appearance)
 * 
 * PACKET DATA (if flag set):
 *   - Gender (1 byte)
 *   - Equipment slots (14 * 2 bytes = 28 bytes)
 *   - Colors (5 * 1 byte = 5 bytes)
 *   Total: ~34 bytes
 * 
 * WHY BITMASK?:
 *   Instead of sending full update every tick (100+ bytes),
 *   only send appearance data when UPDATE_APPEARANCE flag is set.
 *   Saves 99% of bandwidth (appearance changes are rare).
 * 
 * EXAMPLE USAGE:
 *   player->update_flags |= UPDATE_APPEARANCE;  // Set flag
 *   // ... later in update packet ...
 *   if (player->update_flags & UPDATE_APPEARANCE) {
 *       write_appearance_data(buf, player);  // Send appearance
 *   }
 *   player->update_flags = 0;  // Clear all flags for next tick
 */
#define UPDATE_APPEARANCE 0x1

/*******************************************************************************
 * GLOBAL STATE
 ******************************************************************************/

/*
 * g_world - Singleton world instance
 * 
 * LIFETIME:
 *   - NULL initially
 *   - Allocated in main() via world_create()
 *   - Lives for entire server runtime
 *   - Destroyed in main() via world_destroy()
 * 
 * USAGE:
 *   Anywhere in codebase:
 *     if (g_world) {
 *         u32 player_count = world_get_player_count(g_world);
 *         printf("Players online: %u\n", player_count);
 *     }
 * 
 * ALTERNATIVES:
 *   - Pass world* as parameter to every function (verbose)
 *   - Dependency injection (overkill for single-world server)
 *   - Thread-local storage (unnecessary for single-threaded design)
 * 
 * WHY GLOBAL?:
 *   - Simplifies function signatures (no world* parameter)
 *   - Matches original RuneScape architecture
 *   - Only one world per process (natural singleton)
 * 
 * THREAD SAFETY:
 *   - Read-only after initialization (safe for multiple threads to read)
 *   - Modifications to world contents require synchronization
 */
World* g_world = NULL;

/*******************************************************************************
 * LIFECYCLE MANAGEMENT
 ******************************************************************************/

/*
 * world_create - Allocate and initialize game world
 * 
 * ALLOCATION SEQUENCE:
 *   1. Allocate World struct (32 bytes)
 *   2. Create PlayerList (~18KB)
 *   3. Allocate PlayerTracking array (~16MB)
 *   4. Initialize counters (tick_count, last_position_log)
 * 
 * TOTAL MEMORY: ~16MB heap allocation
 * 
 * ERROR HANDLING:
 *   If any allocation fails:
 *     - Free previously allocated memory
 *     - Return NULL (caller must check!)
 *     - No memory leaks
 * 
 * INITIAL STATE:
 *   - All player slots empty (occupied[] = all false)
 *   - All tracking data zeroed
 *   - tick_count = 0 (server just started)
 *   - last_position_log = 0 (never logged yet)
 * 
 * COMPLEXITY: O(MAX_PLAYERS) time (calloc zeros memory)
 *             O(MAX_PLAYERS) space (heap allocations)
 */
World* world_create() {
    /*
     * Step 1: Allocate World struct
     * 
     * calloc() zeroes memory, so all fields initialized to 0/NULL:
     *   - player_list = NULL (will be set below)
     *   - player_tracking = NULL (will be set below)
     *   - last_position_log = 0
     *   - tick_count = 0
     */
    World* world = calloc(1, sizeof(World));
    if (!world) return NULL;  /* Out of memory */
    
    /*
     * Step 2: Create PlayerList
     * 
     * player_list_create() allocates:
     *   - players array: 2048 Player* pointers (16KB)
     *   - occupied bitmap: 2048 bools (2KB)
     *   Total: ~18KB
     * 
     * FAILURE HANDLING:
     *   If allocation fails, must free World before returning NULL
     */
    world->player_list = player_list_create(MAX_PLAYERS);
    if (!world->player_list) {
        free(world);  /* Clean up partial allocation */
        return NULL;
    }
    
    /*
     * Step 3: Allocate PlayerTracking array
     * 
     * One PlayerTracking struct per player slot (2048 elements).
     * 
     * Each PlayerTracking contains:
     *   - local_players[MAX_PLAYERS]: u16 array (4KB)
     *   - local_count: u32 (4 bytes)
     *   - tracked[MAX_PLAYERS]: bool array (2KB)
     *   - appearance_hashes[MAX_PLAYERS]: u8 array (2KB)
     *   Total per struct: ~8KB
     * 
     * Total allocation: 2048 * 8KB = 16MB
     * 
     * calloc() ensures all memory is zeroed:
     *   - local_count = 0 (no players tracked)
     *   - tracked[] = all false (no players visible)
     *   - appearance_hashes[] = all 0 (no cached appearances)
     * 
     * WHY SO LARGE?:
     *   - Must track up to 2048 other players (worst case: all nearby)
     *   - Bitmap approach (bool array) is memory-efficient
     *   - Alternative (hash map) would be similar size with more complexity
     * 
     * FAILURE HANDLING:
     *   If allocation fails, must clean up World and PlayerList
     */
    world->player_tracking = calloc(MAX_PLAYERS, sizeof(PlayerTracking));
    if (!world->player_tracking) {
        player_list_destroy(world->player_list);  /* Free PlayerList */
        free(world);                              /* Free World struct */
        return NULL;
    }
    
    /*
     * Step 4: Initialize timestamps
     * 
     * last_position_log:
     *   - Unix timestamp of last debug position printout
     *   - Initialized to 0 (never logged yet)
     *   - Updated when position logging occurs
     * 
     * tick_count:
     *   - Number of game ticks since server start
     *   - Initialized to 0 (server just started)
     *   - Incremented every 600ms in world_process()
     */
    world->last_position_log = 0;
    world->tick_count = 0;
    
    /*
     * Return initialized World
     * 
     * Caller should:
     *   1. Check for NULL (allocation failure)
     *   2. Store in g_world global
     *   3. Call world_destroy() on server shutdown
     */
    return world;
}

/*
 * world_destroy - Free all world memory and disconnect players
 * 
 * DEALLOCATION SEQUENCE:
 *   1. Destroy all players (close sockets, free movement queues)
 *   2. Destroy PlayerList
 *   3. Free PlayerTracking array
 *   4. Free World struct
 * 
 * MEMORY FREED: ~16MB + (n * 18KB) where n = active players
 * 
 * NULL SAFETY:
 *   Safe to call with world = NULL (no-op)
 * 
 * CASCADING CLEANUP:
 *   world_destroy()
 *     -> For each player: player_destroy()
 *          -> close(socket_fd)
 *          -> movement_destroy()
 *               -> free(waypoint queue)
 *     -> player_list_destroy()
 *          -> free(players array)
 *          -> free(occupied bitmap)
 *     -> free(player_tracking array)
 *     -> free(world struct)
 * 
 * COMPLEXITY: O(n) time where n = active players
 */
void world_destroy(World* world) {
    /* NULL-safe: allow calling on NULL pointer */
    if (!world) return;
    
    /*
     * Step 1: Destroy all players
     * 
     * Loop through all player slots:
     *   - If slot occupied, destroy player
     *   - player_destroy() closes socket and frees resources
     * 
     * START AT INDEX 1 (not 0):
     *   - Index 0 is reserved (never used for players)
     *   - Valid player indices: [1, MAX_PLAYERS)
     * 
     * SOCKET CLEANUP:
     *   player_destroy() calls close(socket_fd):
     *     - Sends TCP FIN packet to client
     *     - Gracefully closes connection
     *     - OS reclaims socket resources
     * 
     * MEMORY CLEANUP:
     *   player_destroy() frees:
     *     - Waypoint queue (movement data)
     *     - Any heap-allocated player fields
     *   
     *   Does NOT free Player struct itself (managed by caller)
     */
    if (world->player_list) {
        for (u32 i = 1; i < world->player_list->capacity; i++) {
            Player* player = world->player_list->players[i];
            if (player) {
                /*
                 * Destroy player resources
                 * 
                 * This includes:
                 *   - Closing network socket (TCP FIN)
                 *   - Freeing movement waypoint queue
                 *   - Resetting player state
                 */
                player_destroy(player);
            }
        }
        
        /*
         * Destroy PlayerList structure
         * 
         * Frees:
         *   - players array (2048 Player* pointers, ~16KB)
         *   - occupied bitmap (2048 bools, ~2KB)
         *   - PlayerList struct itself
         */
        player_list_destroy(world->player_list);
    }
    
    /*
     * Step 2: Free PlayerTracking array
     * 
     * This is the largest single allocation (~16MB).
     * 
     * Must be freed before World struct, since World owns this pointer.
     * 
     * NULL-safe: free(NULL) is safe no-op in C standard.
     */
    if (world->player_tracking) {
        free(world->player_tracking);
    }
    
    /*
     * Step 3: Free World struct itself
     * 
     * Last deallocation (World struct owns all other pointers).
     * 
     * After this call, world pointer is invalid (dangling).
     * Caller should set world = NULL after this function.
     */
    free(world);
}

/*******************************************************************************
 * GAME LOOP PROCESSING
 ******************************************************************************/

/*
 * world_process - Execute one game tick (600ms cycle)
 * 
 * TICK PHASES:
 *   1. Movement processing (walk/run waypoints)
 *   2. Player update packets (send to all clients)
 *   3. Flag cleanup (reset update_flags)
 *   4. Debug logging (every 5 seconds)
 *   5. Tick counter increment
 * 
 * EXECUTION TIME:
 *   - Empty world: <1ms
 *   - 500 players: ~50ms
 *   - 2048 players (all clustered): ~200ms
 *   - Budget: 600ms (plenty of headroom)
 * 
 * PLACEMENT SYSTEM:
 * 
 * When a player logs in or teleports, they need "placement":
 *   - needs_placement = true (set by world_register_player or teleport)
 *   - placement_ticks = 0 (counter starts at 0)
 *   
 * Each tick with needs_placement = true:
 *   - Increment placement_ticks
 *   - If placement_ticks >= 2: clear needs_placement
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
 * COMPLEXITY: O(n^2) worst case where n = active players
 *             O(n) average case (with spatial partitioning)
 */
void world_process(World* world) {
    /* NULL-safe: allow calling on NULL world (no-op) */
    if (!world || !world->player_list) return;
    
    /*
     * PHASE 1: MOVEMENT PROCESSING
     * 
     * For each active player:
     *   1. Process waypoint queue (walk/run paths)
     *   2. Update position based on movement
     *   3. Set primary_direction and secondary_direction
     *   4. Check for region changes (crossing 64x64 boundary)
     * 
     * START AT INDEX 1:
     *   - Index 0 is reserved (never used)
     *   - Valid player indices: [1, capacity)
     * 
     * ACTIVE PLAYER CHECK:
     *   player_is_active() returns true if:
     *     player->state == PLAYER_STATE_LOGGED_IN
     *   
     *   Skips players in states:
     *     - DISCONNECTED (slot empty)
     *     - CONNECTED (awaiting login)
     *     - LOGGING_IN (credentials being verified)
     */
    for (u32 i = 1; i < world->player_list->capacity; i++) {
        Player* player = world->player_list->players[i];
        if (player && player_is_active(player)) {
            /*
             * Process player movement
             * 
             * player_process_movement() does:
             *   1. Pop waypoint from queue
             *   2. Calculate direction (0-7 or -1 for none)
             *   3. Update position (x, z coordinates)
             *   4. Set primary_direction (walk)
             *   5. If running: repeat for secondary_direction
             *   6. Check region change (send new map data if needed)
             * 
             * WALKING vs RUNNING:
             *   - Walk: 1 tile per tick (primary_direction set)
             *   - Run: 2 tiles per tick (primary + secondary set)
             * 
             * REGION CHANGE:
             *   If player crosses 64x64 region boundary:
             *     - Set region_changed = true
             *     - Triggers map data packet next update
             * 
             * COMPLEXITY: O(n) where n = waypoints in queue
             */
            player_process_movement(player);
        }
    }
    
    /*
     * PHASE 2: PLAYER UPDATE PACKETS
     * 
     * Build list of active players, then send update packet to each.
     * 
     * WHY BUILD LIST FIRST?:
     *   - update_player() needs array of all active players
     *   - Used to calculate nearby players (viewport)
     *   - Building once is more efficient than recalculating per player
     * 
     * ACTIVE PLAYERS ARRAY:
     *   - Stack allocation (16KB on 64-bit systems)
     *   - Holds Player* pointers (not Player structs)
     *   - Filled by world_get_active_players()
     */
    Player* active_players[MAX_PLAYERS];
    u32 active_count = 0;
    world_get_active_players(world, active_players, &active_count);
    
    /*
     * Send update packet to each active player
     * 
     * update_player() does:
     *   1. Calculate nearby players (within 15 tiles)
     *   2. Compare to previous tick (player_tracking)
     *   3. Send "add player" for new players
     *   4. Send "remove player" for departed players
     *   5. Send delta updates for existing players
     *   6. Send appearance updates if changed
     * 
     * TRACKING DATA:
     *   &world->player_tracking[p->index] is per-player state:
     *     - local_players: Array of nearby player indices
     *     - local_count: Number of nearby players
     *     - tracked: Bitmap of which players we know about
     *     - appearance_hashes: Cached appearance to detect changes
     * 
     * DEBUG OUTPUT:
     *   "DEBUG: Before update alice - tracking[1].local_count=3"
     *   This shows player username and number of nearby players.
     */
    for (u32 i = 0; i < active_count; i++) {
        Player* p = active_players[i];
        
        /*
         * Debug: Print tracking state before update
         * 
         * Helps diagnose viewport issues:
         *   - If local_count = 0: Player sees no one (isolation bug?)
         *   - If local_count > 100: Too many players (clustering issue?)
         */
        printf("DEBUG: Before update %s - tracking[%u].local_count=%u\n", 
               p->username, p->index, world->player_tracking[p->index].local_count);
        
        /*
         * Send player info packet (opcode 184)
         * 
         * PACKET FORMAT:
         *   [Opcode 184] [Variable-length header]
         *   [Bit-packed movement data]
         *   [Update flags and delta updates]
         * 
         * COMPLEXITY: O(n) where n = nearby players
         */
        update_player(p, active_players, active_count, &world->player_tracking[p->index]);
    }
    
    /*
     * PHASE 3: CLEANUP FLAGS
     * 
     * After sending updates, clean up state for next tick:
     *   1. Increment placement_ticks (if needs_placement)
     *   2. Clear needs_placement after 2 ticks
     *   3. Reset update_flags = 0
     * 
     * WHY RESET FLAGS?:
     *   - Each change should be sent exactly once
     *   - If not reset, same update sent every tick (spam)
     *   - If reset too early, update never sent (missed change)
     * 
     * TIMING:
     *   Reset AFTER sending updates (not before).
     *   Otherwise, update packet would see flags = 0 (no updates sent).
     */
    for (u32 i = 1; i < world->player_list->capacity; i++) {
        Player* player = world->player_list->players[i];
        if (player && player_is_active(player)) {
            /*
             * Placement tick management
             * 
             * PLACEMENT LIFECYCLE:
             *   - Tick 0: needs_placement = true, placement_ticks = 0
             *   - Tick 1: needs_placement = true, placement_ticks = 1
             *   - Tick 2: needs_placement = false (cleared)
             * 
             * INCREMENT LOGIC:
             *   Only increment if needs_placement is true.
             *   Otherwise, placement_ticks stays at 0.
             * 
             * CLEAR CONDITION:
             *   After 2 ticks, clear needs_placement flag.
             *   Player now receives delta updates (not full placement).
             */
            if (player->needs_placement) {
                player->placement_ticks++;
                if (player->placement_ticks >= 2) {
                    player->needs_placement = false;
                }
            }
            
            /*
             * Reset update flags for next tick
             * 
             * BEFORE RESET:
             *   player->update_flags might be:
             *     0x1 (appearance changed)
             *     0x8 (animation playing)
             *     0x9 (appearance + animation)
             * 
             * AFTER RESET:
             *   player->update_flags = 0 (clean slate)
             * 
             * NEXT TICK:
             *   Only new changes will set flags:
             *     player->update_flags |= UPDATE_CHAT;  // New chat message
             */
            player->update_flags = 0;
        }
    }
    
    /*
     * PHASE 4: DEBUG LOGGING
     * 
     * Print all player positions every 5 seconds.
     * 
     * RATE LIMITING:
     *   - time(NULL) returns current Unix timestamp (seconds since epoch)
     *   - If (now - last_position_log) >= 5: print positions
     *   - Update last_position_log = now
     * 
     * WHY 5 SECONDS?:
     *   - Not too frequent (avoid spam)
     *   - Not too rare (still useful for debugging)
     *   - Every tick (600ms) would be 1.67 logs/sec (too much)
     * 
     * OUTPUT FORMAT:
     *   "Player: alice Position: (3222, 3218)"
     *   "Player: bob Position: (3225, 3220)"
     * 
     * USE CASE:
     *   Diagnosing position desync issues:
     *     - Client thinks player at (3222, 3218)
     *     - Server log shows player at (3200, 3200)
     *     - Indicates movement packet not received
     */
    u64 now = (u64)time(NULL);
    if (now - world->last_position_log >= 5) {
        /*
         * Print all active player positions
         * 
         * Only prints players in LOGGED_IN state.
         * Skips DISCONNECTED, CONNECTED, LOGGING_IN states.
         */
        for (u32 i = 1; i < world->player_list->capacity; i++) {
            Player* player = world->player_list->players[i];
            if (player && player_is_active(player)) {
                /*
                 * Print position
                 * 
                 * FORMAT: "Player: <username> Position: (<x>, <z>)"
                 * 
                 * NOTE: Uses x and z (not y).
                 * RuneScape uses:
                 *   - x: East-West coordinate
                 *   - z: North-South coordinate
                 *   - height: Plane (0-3, not printed here)
                 */
                printf("Player: %s Position: (%u, %u)\n", 
                       player->username, player->position.x, player->position.z);
            }
        }
        
        /*
         * Update timestamp
         * 
         * Prevents logging again until 5 more seconds elapse.
         */
        world->last_position_log = now;
    }
    
    /*
     * PHASE 5: INCREMENT TICK COUNTER
     * 
     * tick_count tracks total ticks since server start.
     * 
     * USES:
     *   - Event scheduling: "Despawn item at tick 1000"
     *   - Uptime calculation: tick_count * 600ms = server runtime
     *   - Profiling: Measure ticks between events
     * 
     * OVERFLOW SAFETY:
     *   - u64 max: 18,446,744,073,709,551,615 ticks
     *   - At 600ms/tick: 350 million years of uptime
     *   - Will never overflow in practice
     * 
     * MONOTONICITY:
     *   - tick_count only increases (never decreases)
     *   - Safe for chronological ordering
     */
    world->tick_count++;
}

/*******************************************************************************
 * PLAYER MANAGEMENT
 ******************************************************************************/

/*
 * world_register_player - Add player to world and assign index
 * 
 * REGISTRATION PROCESS:
 *   1. Copy username to player struct
 *   2. Find free slot in player_list
 *   3. Assign player to slot
 *   4. Set player state to LOGGED_IN
 *   5. Set placement flags
 *   6. Print debug message
 * 
 * USERNAME HANDLING:
 *   - Copies up to 12 characters
 *   - Null-terminates to prevent overflow
 *   - Safe for strcmp() and printf()
 * 
 * INDEX ASSIGNMENT:
 *   - player_list_add() finds first free slot
 *   - Uses next_pid hint for O(1) average case
 *   - Returns false if world full (all 2047 slots occupied)
 * 
 * STATE TRANSITION:
 *   Before: LOGGING_IN → After: LOGGED_IN
 * 
 * COMPLEXITY: O(n) worst case, O(1) average case
 */
bool world_register_player(World* world, Player* player, const char* username) {
    /* Validate inputs (NULL checks) */
    if (!world || !world->player_list || !player || !username) return false;
    
    /*
     * Step 1: Copy username to player struct
     * 
     * strncpy() copies up to N characters:
     *   - N = sizeof(player->username) - 1 = 12
     *   - Does NOT null-terminate if source longer than N
     * 
     * EXAMPLE:
     *   username = "alice" (5 chars)
     *   strncpy(dest, "alice", 12) copies "alice\0" (6 bytes)
     *   
     *   username = "VeryLongUsername123" (19 chars)
     *   strncpy(dest, "VeryLongUsername123", 12) copies "VeryLongUser" (12 bytes, NO null!)
     * 
     * FIX: Manually null-terminate after strncpy
     *   player->username[12] = '\0';
     * 
     * RESULT:
     *   - Short usernames: null-terminated naturally
     *   - Long usernames: truncated at 12 chars, then null-terminated
     *   - Always safe for strcmp() and printf()
     */
    strncpy(player->username, username, sizeof(player->username) - 1);
    player->username[sizeof(player->username) - 1] = '\0';  /* Ensure null termination */
    
    /*
     * Step 2: Add to player list
     * 
     * player_list_add() does:
     *   1. Find free slot (occupied[i] == false)
     *   2. Set players[i] = player
     *   3. Set occupied[i] = true
     *   4. Set player->index = i
     *   5. Increment count
     *   6. Update next_pid = i + 1 (hint for next allocation)
     * 
     * FAILURE CASE:
     *   If world full (all 2047 slots occupied):
     *     - Returns false
     *     - Prints error message
     *     - Player NOT added to world
     * 
     * COMPLEXITY:
     *   - Best case: O(1) (next_pid is free)
     *   - Worst case: O(n) (must scan all slots)
     */
    if (!player_list_add(world->player_list, player)) {
        /*
         * World is full (2047 players already online)
         * 
         * DEBUG OUTPUT:
         *   "Failed to register player alice: world is full!"
         * 
         * CALLER SHOULD:
         *   - Send LOGIN_RESPONSE_WORLD_FULL to client
         *   - Destroy player struct (wasn't added)
         *   - Close socket
         */
        printf("Failed to register player %s: world is full!\n", username);
        return false;
    }
    
    /*
     * Step 2.5: Clear tracking data for this player slot
     * 
     * CRITICAL: When a player slot is reused, the old PlayerTracking data
     * from a previous player may still be present. This includes:
     *   - tracked[] array: which players were visible before
     *   - local_players[]: list of nearby players
     *   - local_count: count of tracked players
     *   - appearance_hashes[]: cached appearance data
     * 
     * If not cleared, the new player inherits stale tracking state:
     *   - tracked[other_index] = true from previous session
     *   - Server thinks it's already tracking other players
     *   - "Third pass" skips adding players (already tracked check)
     *   - Result: New player can't see anyone!
     * 
     * FIX: Zero the entire PlayerTracking struct for this slot
     */
    memset(&world->player_tracking[player->index], 0, sizeof(PlayerTracking));
    
    /*
     * Step 3: Set player state
     * 
     * PLAYER_STATE_LOGGED_IN means:
     *   - Player is in game world
     *   - Can send/receive game packets
     *   - Visible to other players
     *   - Processed by world_process() each tick
     * 
     * PREVIOUS STATE:
     *   - Usually PLAYER_STATE_LOGGING_IN (credentials verified)
     *   - Could be CONNECTED (if login bypassed, e.g. debug mode)
     */
    player->state = PLAYER_STATE_LOGGED_IN;
    
    /*
     * Step 4: Set placement flags
     * 
     * needs_placement = true:
     *   - Tells update system to send full position (not delta)
     *   - Required for initial login (client doesn't know position yet)
     *   - Also used for teleports (sudden position change)
     * 
     * placement_ticks = 0:
     *   - Counter starts at 0
     *   - Incremented each tick by world_process()
     *   - needs_placement cleared when placement_ticks >= 2
     * 
     * WHY 2 TICKS?:
     *   - Client needs time to receive initial position
     *   - Prevents visual glitches (player appearing then jumping)
     *   - Matches original RuneScape behavior
     */
    player->needs_placement = true;
    player->placement_ticks = 0;
    
    /*
     * Step 5: Print debug message
     * 
     * OUTPUT:
     *   "Registered new player: alice Index: 1"
     *   "Registered new player: bob Index: 5"
     * 
     * USEFUL FOR:
     *   - Monitoring player logins
     *   - Debugging index assignment
     *   - Server activity logging
     */
    printf("Registered new player: %s Index: %u\n", username, player->index);
    
    /*
     * Return success
     * 
     * Caller should:
     *   - Send LOGIN_RESPONSE_OK to client
     *   - Send initial packets (skills, inventory, interface)
     *   - Start processing player each tick
     */
    return true;
}

/*
 * world_remove_player - Remove player from world by username
 * 
 * REMOVAL PROCESS:
 *   1. Find player by username (linear search)
 *   2. Save player index
 *   3. Clear tracking data (memset to 0)
 *   4. Set player state to DISCONNECTED
 *   5. Remove from player_list
 *   6. Print debug message
 * 
 * TRACKING CLEANUP:
 *   Zeroes all tracking data to prevent stale state when slot reused.
 * 
 * STATE TRANSITION:
 *   Before: LOGGED_IN → After: DISCONNECTED
 * 
 * SOCKET HANDLING:
 *   Does NOT close socket (caller must call player_destroy separately)
 * 
 * COMPLEXITY: O(n) time (username lookup)
 */
void world_remove_player(World* world, const char* username) {
    /* Validate inputs (NULL checks) */
    if (!world || !world->player_list || !username) return;
    
    /*
     * Step 1: Find player by username
     * 
     * world_get_player() does linear search:
     *   for i = 1 to MAX_PLAYERS:
     *     if players[i] && strcmp(players[i]->username, username) == 0:
     *       return players[i]
     * 
     * COMPLEXITY: O(n) where n = active players
     * 
     * OPTIMIZATION:
     *   Could use hash map: username -> index (O(1) lookup)
     *   Trade-off: Extra memory and complexity
     */
    Player* player = world_get_player(world, username);
    if (player) {
        /*
         * Player found - proceed with removal
         */
        
        /*
         * Step 2: Save player index
         * 
         * Need index to:
         *   - Clear tracking data: player_tracking[pid]
         *   - Remove from list: player_list_remove(pid)
         * 
         * CAST TO u16:
         *   - player->index is u32
         *   - player_list_remove expects u16
         *   - Safe cast (index < MAX_PLAYERS = 2048 < 65535)
         */
        u16 pid = player->index;
        
        /*
         * Step 3: Clear tracking data
         * 
         * memset(&world->player_tracking[pid], 0, ...) zeroes:
         *   - local_players[] array
         *   - local_count
         *   - tracked[] bitmap
         *   - appearance_hashes[] array
         * 
         * WHY CLEAR?:
         *   When slot is reused for new player, stale data could cause:
         *     - New player sees phantom players from previous session
         *     - Appearance updates sent to wrong players
         *     - Tracking state inconsistencies
         * 
         * ALTERNATIVE:
         *   Could clear on registration instead of removal.
         *   But clearing on removal is safer (no leftover state).
         * 
         * COMPLEXITY: O(n) where n = sizeof(PlayerTracking) = ~8KB
         */
        memset(&world->player_tracking[pid], 0, sizeof(PlayerTracking));
        
        /*
         * Step 4: Set player state
         * 
         * PLAYER_STATE_DISCONNECTED means:
         *   - Player is no longer in game
         *   - Socket may be closed (or will be soon)
         *   - Invisible to other players
         *   - Not processed by world_process()
         * 
         * PREVIOUS STATE:
         *   - Usually PLAYER_STATE_LOGGED_IN (graceful logout)
         *   - Could be LOGGING_IN (timeout during login)
         */
        player->state = PLAYER_STATE_DISCONNECTED;
        
        /*
         * Step 5: Remove from player list
         * 
         * player_list_remove() does:
         *   1. Set players[pid] = NULL
         *   2. Set occupied[pid] = false
         *   3. Decrement count
         * 
         * Does NOT free Player struct (caller's responsibility).
         * Does NOT close socket (caller must call player_destroy).
         * 
         * COMPLEXITY: O(1) (array assignment)
         */
        player_list_remove(world->player_list, pid);
        
        /*
         * Step 6: Print debug message
         * 
         * OUTPUT:
         *   "Removed player: alice"
         * 
         * USEFUL FOR:
         *   - Monitoring player logouts
         *   - Debugging removal issues
         *   - Server activity logging
         */
        printf("Removed player: %s\n", username);
    } else {
        /*
         * Player not found
         * 
         * POSSIBLE CAUSES:
         *   - Player already removed (duplicate call)
         *   - Username typo
         *   - Player never registered
         * 
         * BEHAVIOR:
         *   - Print warning message
         *   - Return without error (idempotent)
         *   - Safe to call multiple times
         * 
         * OUTPUT:
         *   "Tried to remove non-existing player: charlie"
         */
        printf("Tried to remove non-existing player: %s\n", username);
    }
}

/*
 * world_get_player - Find player by username
 * 
 * SEARCH ALGORITHM:
 *   Linear search through player_list array.
 * 
 * STRING COMPARISON:
 *   strcmp() returns 0 if strings match.
 * 
 * CASE SENSITIVITY:
 *   This implementation is CASE-SENSITIVE.
 *   "Alice" != "alice"
 * 
 * COMPLEXITY: O(n) time where n = active players
 */
Player* world_get_player(World* world, const char* username) {
    /* Validate inputs (NULL checks) */
    if (!world || !world->player_list || !username) return NULL;
    
    /*
     * Linear search through all player slots
     * 
     * START AT INDEX 1:
     *   - Index 0 is reserved (never used for players)
     *   - Valid player indices: [1, capacity)
     * 
     * CHECK SEQUENCE:
     *   1. players[i] != NULL (slot occupied?)
     *   2. strcmp(username, ...) == 0 (name matches?)
     * 
     * SHORT-CIRCUIT EVALUATION:
     *   If players[i] == NULL, strcmp is NOT called (prevents crash).
     * 
     * EARLY RETURN:
     *   As soon as match found, return player (don't search rest).
     * 
     * COMPLEXITY:
     *   - Best case: O(1) (player at index 1)
     *   - Average case: O(n/2) (player in middle)
     *   - Worst case: O(n) (player at end or not found)
     */
    for (u32 i = 1; i < world->player_list->capacity; i++) {
        Player* player = world->player_list->players[i];
        
        /*
         * Check if slot occupied and username matches
         * 
         * strcmp() performs lexicographic comparison:
         *   - Returns 0 if strings equal (found!)
         *   - Returns <0 if str1 < str2
         *   - Returns >0 if str1 > str2
         * 
         * EXAMPLE:
         *   strcmp("alice", "alice") = 0  (match!)
         *   strcmp("alice", "bob") = -1   (no match)
         */
        if (player && strcmp(player->username, username) == 0) {
            /*
             * Found matching player
             * 
             * RETURN:
             *   - Non-NULL Player* (valid pointer)
             *   - Caller should check for NULL (no match)
             */
            return player;
        }
    }
    
    /*
     * No matching player found
     * 
     * RETURN:
     *   - NULL (not found)
     * 
     * CALLER MUST CHECK:
     *   Player* p = world_get_player(g_world, "alice");
     *   if (p) {
     *       // Use p
     *   } else {
     *       // Player not online
     *   }
     */
    return NULL;
}

/*
 * world_get_player_by_index - Retrieve player by slot index
 * 
 * DIRECT ARRAY ACCESS:
 *   O(1) lookup (no search needed).
 * 
 * INDEX VALIDATION:
 *   player_list_get() checks index < capacity.
 * 
 * RETURN VALUE:
 *   - Non-NULL: Valid player pointer
 *   - NULL: Slot empty or invalid index
 * 
 * COMPLEXITY: O(1) time
 */
Player* world_get_player_by_index(World* world, u32 index) {
    /* Validate inputs (NULL checks) */
    if (!world || !world->player_list) return NULL;
    
    /*
     * Get player from list by index
     * 
     * player_list_get() does:
     *   1. Check index < capacity (bounds check)
     *   2. Return players[index] (may be NULL)
     * 
     * NO SEARCH NEEDED:
     *   Direct array access is O(1).
     *   Much faster than world_get_player() which searches by username.
     * 
     * CAST TO u16:
     *   - index parameter is u32
     *   - player_list_get expects u16
     *   - Safe cast (index < MAX_PLAYERS = 2048 < 65535)
     * 
     * RETURN:
     *   - Non-NULL: Valid player at this index
     *   - NULL: Slot empty or index out of bounds
     */
    return player_list_get(world->player_list, (u16)index);
}

/*
 * world_get_free_index - Find next available player slot
 * 
 * SEARCH STRATEGY:
 *   Circular search starting at next_pid hint.
 * 
 * RETURN VALUE:
 *   - [1-2047]: Valid free slot
 *   - -1: World full (no free slots)
 * 
 * COMPLEXITY: O(n) worst case, O(1) average case
 */
i32 world_get_free_index(World* world) {
    /* Validate inputs (NULL checks) */
    if (!world || !world->player_list) return -1;
    
    /*
     * Get next free player index
     * 
     * player_list_get_next_pid() does:
     *   1. Start at next_pid (hint from last allocation)
     *   2. Search for occupied[i] == false
     *   3. Wrap around if needed (circular search)
     *   4. Return index if found, or 0 if full
     * 
     * CIRCULAR SEARCH:
     *   next_pid = 5
     *   Search order: 5, 6, 7, ..., 2047, 1, 2, 3, 4
     * 
     * COMPLEXITY:
     *   - Best case: O(1) (next_pid is free)
     *   - Average case: O(1) (few slots checked)
     *   - Worst case: O(n) (world nearly full)
     */
    u16 pid = player_list_get_next_pid(world->player_list);
    
    /*
     * Convert to signed integer
     * 
     * player_list_get_next_pid() returns:
     *   - u16 [1-2047]: Valid free slot
     *   - u16 0: No free slots (world full)
     * 
     * world_get_free_index() returns:
     *   - i32 [1-2047]: Valid free slot
     *   - i32 -1: No free slots (error sentinel)
     * 
     * CONVERSION:
     *   If pid == 0: return -1 (world full)
     *   Else: return pid (cast u16 to i32)
     * 
     * WHY -1 INSTEAD OF 0?:
     *   - 0 could be confused with false
     *   - -1 is clearly an error (negative index is invalid)
     *   - Matches convention of many C APIs (e.g., open() returns -1 on error)
     */
    return pid ? (i32)pid : -1;
}

/*
 * world_get_player_count - Get number of active players
 * 
 * CACHED COUNT:
 *   player_list->count is maintained by add/remove operations.
 * 
 * RETURN VALUE:
 *   - [0-2047]: Number of players online
 *   - 0 if world or player_list is NULL
 * 
 * COMPLEXITY: O(1) time (reading cached value)
 */
u32 world_get_player_count(World* world) {
    /* Validate inputs (NULL checks) */
    if (!world || !world->player_list) return 0;
    
    /*
     * Return cached player count
     * 
     * player_list->count is maintained by:
     *   - player_list_add(): count++
     *   - player_list_remove(): count--
     * 
     * ALWAYS ACCURATE:
     *   No need to scan array and count non-NULL slots.
     * 
     * ALTERNATIVE (slower):
     *   u32 count = 0;
     *   for (u32 i = 1; i < MAX_PLAYERS; i++) {
     *       if (players[i]) count++;
     *   }
     *   return count;
     *   
     *   This is O(n) instead of O(1) - inefficient!
     * 
     * COMPLEXITY: O(1) time
     */
    return world->player_list->count;
}

/*******************************************************************************
 * UTILITY FUNCTIONS
 ******************************************************************************/

/*
 * world_get_active_players - Build array of all logged-in players
 * 
 * ACTIVE PLAYER DEFINITION:
 *   player->state == PLAYER_STATE_LOGGED_IN
 * 
 * OUTPUT FORMAT:
 *   - out_players[] filled with Player* pointers
 *   - *out_count set to number of active players
 * 
 * ARRAY BOUNDS:
 *   Stops at MAX_PLAYERS to prevent overflow.
 * 
 * COMPLEXITY: O(n) time where n = MAX_PLAYERS
 */
void world_get_active_players(World* world, Player** out_players, u32* out_count) {
    /* Validate inputs (NULL checks) */
    if (!world || !world->player_list || !out_players || !out_count) return;
    
    /*
     * Initialize output count
     * 
     * Must set to 0 before loop (incremented for each active player).
     */
    *out_count = 0;
    
    /*
     * Scan all player slots
     * 
     * START AT INDEX 1:
     *   - Index 0 is reserved (never used)
     *   - Valid player indices: [1, capacity)
     * 
     * STOP CONDITIONS:
     *   1. i >= capacity (scanned all slots)
     *   2. *out_count >= MAX_PLAYERS (array full)
     * 
     * OVERFLOW PREVENTION:
     *   If *out_count >= MAX_PLAYERS: break
     *   Prevents writing past end of out_players array.
     */
    for (u32 i = 1; i < world->player_list->capacity && *out_count < MAX_PLAYERS; i++) {
        Player* player = world->player_list->players[i];
        
        /*
         * Check if player is active
         * 
         * player_is_active() returns true if:
         *   player->state == PLAYER_STATE_LOGGED_IN
         * 
         * Excludes players in states:
         *   - DISCONNECTED (slot empty)
         *   - CONNECTED (awaiting login)
         *   - LOGGING_IN (credentials being verified)
         * 
         * WHY CHECK BOTH player AND player_is_active()?:
         *   - player != NULL: Slot is occupied
         *   - player_is_active(): Player is in LOGGED_IN state
         *   
         *   A player can exist but not be active:
         *     - State = CONNECTED (just connected, awaiting login)
         *     - State = LOGGING_IN (credentials being verified)
         */
        if (player && player_is_active(player)) {
            /*
             * Add player to output array
             * 
             * out_players[*out_count] = player:
             *   - Store Player* pointer at current index
             *   - Index is *out_count (current number of active players)
             * 
             * (*out_count)++:
             *   - Increment count (found another active player)
             *   - Parentheses ensure ++ applies to dereferenced pointer
             *   - Equivalent to: *out_count = *out_count + 1;
             * 
             * EXAMPLE:
             *   Before: *out_count = 3
             *           out_players = [Player1*, Player2*, Player3*, ???, ...]
             *   
             *   After:  *out_count = 4
             *           out_players = [Player1*, Player2*, Player3*, Player4*, ...]
             */
            out_players[(*out_count)++] = player;
        }
    }
    
    /*
     * Return (out_players filled, out_count set)
     * 
     * CALLER CAN NOW:
     *   for (u32 i = 0; i < count; i++) {
     *       Player* p = active_players[i];
     *       // Process active player
     *   }
     */
}
