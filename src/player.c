/*******************************************************************************
 * PLAYER.C - Player Entity Implementation
 *******************************************************************************
 * 
 * This file implements player entity management including initialization,
 * cleanup, movement processing, and state transitions.
 * 
 * KEY ALGORITHMS IMPLEMENTED:
 *   1. Player lifecycle management (init, destroy, disconnect)
 *   2. Socket lifecycle (cross-platform close)
 *   3. Movement processing (walk + run in single tick)
 *   4. Region change detection (viewport/chunk management)
 *   5. Position synchronization (placement flags)
 * 
 * EDUCATIONAL CONCEPTS:
 *   - Resource management (socket closing, memory cleanup)
 *   - State machine implementation (connection states)
 *   - Entity update loops (game tick processing)
 *   - Spatial partitioning (region system)
 *   - Platform abstraction (Windows vs POSIX sockets)
 * 
 * CROSS-PLATFORM SOCKET MANAGEMENT:
 * 
 * Windows (Winsock):
 *   #include <winsock2.h>
 *   closesocket(fd);  // Close socket
 * 
 * POSIX (Linux, macOS, BSD):
 *   #include <unistd.h>
 *   close(fd);  // Close socket (same as file descriptor)
 * 
 * Why different?
 *   - Windows treats sockets as SOCKET type (not int)
 *   - POSIX unifies files and sockets under same fd abstraction
 *   - closesocket() is Winsock-specific, close() is POSIX standard
 * 
 ******************************************************************************/

#include "player.h"
#include "player_save.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "map.h"
#ifdef _WIN32
#include <winsock2.h>   /* Windows socket API */
#else
#include <unistd.h>     /* POSIX close() */
#endif

/*******************************************************************************
 * INITIALIZATION AND CLEANUP
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
 *   3. Set socket_fd: player->socket_fd = -1 (no connection)
 *   4. Set state: player->state = PLAYER_STATE_DISCONNECTED
 *   5. Initialize position: position_init(&player->position, 3222, 3218, 0)
 *   6. Initialize movement: movement_init(&player->movement)
 *   7. Set directions: primary_direction = secondary_direction = -1 (not moving)
 *   8. Set placement_ticks: 0
 * 
 * SPAWN POSITION (3222, 3218, 0):
 *   Location: Lumbridge Castle courtyard
 *   Region: (50, 50)
 *   Height: Ground floor (0)
 *   
 *   Why Lumbridge?
 *     - Default spawn in RuneScape
 *     - Safe area (no aggressive NPCs)
 *     - Central location for new players
 * 
 * ZEROED FIELDS (via memset):
 *   ┌─────────────────────────────────────────┐
 *   │ username           = "" (null bytes)    │
 *   │ password           = "" (null bytes)    │
 *   │ in_cipher          = (zeroed ISAAC)     │
 *   │ out_cipher         = (zeroed ISAAC)     │
 *   │ needs_placement    = false (0)          │
 *   │ teleporting        = false (0)          │
 *   │ region_changed     = false (0)          │
 *   │ in_buffer          = (zeroed bytes)     │
 *   │ in_buffer_size     = 0                  │
 *   │ out_buffer         = (zeroed bytes)     │
 *   │ out_buffer_size    = 0                  │
 *   │ update_flags       = 0                  │
 *   │ login_time         = 0                  │
 *   └─────────────────────────────────────────┘
 * 
 * EXPLICITLY SET FIELDS:
 *   index              = <provided parameter>
 *   socket_fd          = -1 (INVALID_SOCKET marker)
 *   state              = PLAYER_STATE_DISCONNECTED
 *   position           = {3222, 3218, 0}
 *   movement           = {empty queue, run_energy=10000}
 *   primary_direction  = -1 (not moving)
 *   secondary_direction= -1 (not running)
 *   placement_ticks    = 0
 * 
 * WHY memset BEFORE FIELD INITIALIZATION?
 *   - Guarantees all padding bytes are zero (reproducible memory state)
 *   - Safer than field-by-field (can't forget a field)
 *   - Faster (single CPU instruction on most architectures)
 *   - Then override specific fields that need non-zero values
 * 
 * SOCKET FD = -1:
 *   Convention: -1 represents "invalid file descriptor"
 *   All POSIX functions return -1 on error (socket(), accept(), etc.)
 *   Checking socket_fd >= 0 determines if socket is valid
 * 
 * USAGE - SERVER STARTUP:
 *   Player players[MAX_PLAYERS];
 *   
 *   void init_server() {
 *     for (u32 i = 0; i < MAX_PLAYERS; i++) {
 *       player_init(&players[i], i);
 *     }
 *     printf("Initialized %u player slots\n", MAX_PLAYERS);
 *   }
 * 
 * COMPLEXITY: O(1) time - memset is constant for fixed struct size
 */
void player_init(Player* player, u32 index) {
    memset(player, 0, sizeof(Player));
    player->index = index;
    player->socket_fd = -1;
    player->state = PLAYER_STATE_DISCONNECTED;
    position_init(&player->position, 3222, 3218, 0);
    movement_init(&player->movement);
    player->primary_direction = -1;
    player->secondary_direction = -1;
    player->placement_ticks = 0;
}

/*
 * player_destroy - Clean up player resources
 * 
 * @param player  Pointer to Player to clean up
 * 
 * ALGORITHM:
 *   1. Free movement waypoints: movement_destroy(&player->movement)
 *   2. If socket valid (socket_fd >= 0):
 *        a. Close socket: close(socket_fd) or closesocket(socket_fd)
 *        b. Set socket_fd = -1 (mark as closed)
 * 
 * RESOURCE CLEANUP:
 *   1. Movement queue:
 *        - Frees all heap-allocated Point structs
 *        - Prevents memory leaks from queued waypoints
 *   
 *   2. TCP socket:
 *        - Closes connection (sends FIN to client)
 *        - Releases OS resources (file descriptor on POSIX, SOCKET handle on Windows)
 *        - After close(), fd can be reused by next accept()
 * 
 * PLATFORM-SPECIFIC SOCKET CLOSING:
 *   
 *   Windows (Winsock):
 *     closesocket(player->socket_fd);
 *     - Winsock-specific function
 *     - SOCKET type (unsigned int)
 *     - Requires winsock2.h
 *   
 *   POSIX (Linux, macOS):
 *     close(player->socket_fd);
 *     - Standard POSIX file descriptor close
 *     - int type
 *     - Requires unistd.h
 * 
 * TCP CONNECTION TEARDOWN (Four-Way Handshake):
 *   When close() is called on a TCP socket:
 *   
 *   Server (us)          Client
 *      │                   │
 *      │─────── FIN ──────>│  1. Server sends FIN (finish)
 *      │                   │     "I'm done sending data"
 *      │                   │
 *      │<────── ACK ───────│  2. Client acknowledges FIN
 *      │                   │
 *      │<────── FIN ───────│  3. Client sends FIN
 *      │                   │     "I'm done too"
 *      │                   │
 *      │─────── ACK ──────>│  4. Server acknowledges FIN
 *      │                   │
 *   Connection closed    Connection closed
 * 
 * TIME-WAIT STATE:
 *   After close(), socket enters TIME_WAIT (typically 60-120 seconds)
 *   Prevents old duplicate packets from confusing new connections
 *   OS holds socket in this state, fd is immediately reusable
 * 
 * SOCKET FD = -1 AFTER CLOSE:
 *   Sets socket_fd to -1 to mark as invalid
 *   Prevents double-close (calling close() on same fd twice is undefined)
 *   player_is_active() will return false
 * 
 * DOES NOT ZERO STRUCT:
 *   Does NOT memset() entire struct
 *   Preserves username, login_time, etc. for logging/analytics
 *   Call player_init() to fully reset for reuse
 * 
 * WHEN TO CALL:
 *   1. Player logout:
 *        save_player(player);
 *        player_destroy(player);
 *        player_init(player, player->index);  // Reset for reuse
 *   
 *   2. Player timeout:
 *        log_disconnect(player, "Timeout");
 *        player_destroy(player);
 *   
 *   3. Server shutdown:
 *        for (u32 i = 0; i < MAX_PLAYERS; i++) {
 *          if (players[i].socket_fd >= 0) {
 *            player_destroy(&players[i]);
 *          }
 *        }
 * 
 * COMPLEXITY: O(n) time where n = movement.waypoint_count
 */
void player_destroy(Player* player) {
    movement_destroy(&player->movement);
    if (player->socket_fd >= 0) {
#ifdef _WIN32
        closesocket(player->socket_fd);
#else
        close(player->socket_fd);
#endif
        player->socket_fd = -1;
    }
}

/*
 * player_disconnect - Disconnect player and reset state
 * 
 * @param player  Pointer to Player to disconnect
 * 
 * ALGORITHM:
 *   1. Set state to DISCONNECTED: player->state = PLAYER_STATE_DISCONNECTED
 *   2. Clean up resources: player_destroy(player)
 * 
 * STATE TRANSITION:
 *   (any state) → PLAYER_STATE_DISCONNECTED
 * 
 * DIFFERENCE FROM player_destroy:
 *   ┌─────────────────────┬──────────────────────────────────┐
 *   │ player_destroy      │ player_disconnect                │
 *   ├─────────────────────┼──────────────────────────────────┤
 *   │ Frees resources     │ Sets state, then frees resources │
 *   │ Closes socket       │ Same                             │
 *   │ Does NOT change     │ Sets state = DISCONNECTED        │
 *   │   state             │                                  │
 *   └─────────────────────┴──────────────────────────────────┘
 * 
 * WHY SET STATE BEFORE destroy?
 *   Order matters for concurrent access (though this code is single-threaded):
 *     1. Set state = DISCONNECTED  → Other systems see player as offline
 *     2. Call player_destroy()     → Clean up resources
 *   
 *   If reversed:
 *     1. player_destroy()          → Socket closed
 *     2. state = DISCONNECTED      → Brief window where state is wrong
 *   
 *   Setting state first is safer (fail-fast principle)
 * 
 * SIDE EFFECTS:
 *   - state = PLAYER_STATE_DISCONNECTED
 *   - socket_fd = -1 (via player_destroy)
 *   - movement queue cleared (via player_destroy)
 *   - Client receives TCP FIN packet
 *   - Player becomes invisible in update loop:
 *       if (player_is_active(&player))  → false
 * 
 * USAGE - KICK PLAYER:
 *   void kick_player(Player* player, const char* reason) {
 *     send_kick_message(player, reason);  // Send packet before disconnect
 *     player_disconnect(player);
 *     printf("Kicked player %s: %s\n", player->username, reason);
 *   }
 * 
 * USAGE - GRACEFUL LOGOUT:
 *   void handle_logout_packet(Player* player) {
 *     save_player_data(player);
 *     send_logout_response(player);  // Confirm logout
 *     player_disconnect(player);
 *   }
 * 
 * COMPLEXITY: O(n) time where n = movement.waypoint_count
 */
void player_disconnect(Player* player) {
    /* Save player data if they were logged in */
    if (player->state == PLAYER_STATE_LOGGED_IN && player->username[0] != '\0') {
        printf("Saving player '%s' before disconnect...\n", player->username);
        if (!player_save(player)) {
            printf("WARNING: Failed to save player '%s'\n", player->username);
        }
    }
    
    player->state = PLAYER_STATE_DISCONNECTED;
    player_destroy(player);
}

/*******************************************************************************
 * CONNECTION MANAGEMENT
 ******************************************************************************/

/*
 * player_set_socket - Assign socket to player and mark connected
 * 
 * @param player     Pointer to Player
 * @param socket_fd  TCP socket file descriptor (>= 0)
 * 
 * ALGORITHM:
 *   1. Set socket: player->socket_fd = socket_fd
 *   2. Set state: player->state = PLAYER_STATE_CONNECTED
 * 
 * STATE TRANSITION:
 *   PLAYER_STATE_DISCONNECTED → PLAYER_STATE_CONNECTED
 * 
 * PRECONDITIONS:
 *   - socket_fd >= 0 (valid file descriptor from accept())
 *   - player->state == DISCONNECTED (slot is available)
 * 
 * POSTCONDITIONS:
 *   - player->socket_fd == socket_fd
 *   - player->state == CONNECTED
 *   - Player can now receive packets (recv() on socket_fd)
 * 
 * WHEN CALLED:
 *   After accept() in main server loop:
 * 
 *   Server accept loop:
 *     while (server_running) {
 *       int client_fd = accept(server_socket, &client_addr, &addr_len);
 *       
 *       if (client_fd < 0) {
 *         perror("accept");
 *         continue;
 *       }
 *       
 *       // Find free player slot
 *       Player* free_slot = NULL;
 *       for (u32 i = 0; i < MAX_PLAYERS; i++) {
 *         if (players[i].state == PLAYER_STATE_DISCONNECTED) {
 *           free_slot = &players[i];
 *           break;
 *         }
 *       }
 *       
 *       if (free_slot) {
 *         player_set_socket(free_slot, client_fd);
 *         printf("Player %u connected (fd=%d)\n", free_slot->index, client_fd);
 *       } else {
 *         // Server full
 *         send_server_full_message(client_fd);
 *         close(client_fd);
 *       }
 *     }
 * 
 * CONNECTED STATE BEHAVIOR:
 *   Player is now awaiting login packet
 *   Timeout: 10 seconds (if no login packet, disconnect)
 *   
 *   Next packet handler should:
 *     1. Read login packet (username, password, version)
 *     2. Transition to LOGGING_IN state
 *     3. Verify credentials
 *     4. Transition to LOGGED_IN on success
 * 
 * COMPLEXITY: O(1) time
 */
void player_set_socket(Player* player, i32 socket_fd) {
    player->socket_fd = socket_fd;
    player->state = PLAYER_STATE_CONNECTED;
}

/*******************************************************************************
 * POSITION MANAGEMENT
 ******************************************************************************/

/*
 * player_set_position - Update player position and check region change
 * 
 * @param player  Pointer to Player
 * @param x       New world X coordinate
 * @param z       New world Z coordinate
 * @param height  New height plane [0-3]
 * 
 * ALGORITHM:
 *   1. Get old region:
 *        old_region_x = position_get_region_x(&player->position)
 *        old_region_z = position_get_region_z(&player->position)
 *   2. Update position:
 *        position_init(&player->position, x, z, height)
 *   3. Get new region:
 *        new_region_x = position_get_region_x(&player->position)
 *        new_region_z = position_get_region_z(&player->position)
 *   4. Check region change:
 *        if (old_region_x != new_region_x || old_region_z != new_region_z)
 *          player->region_changed = true
 *   5. Set placement flag:
 *        player->needs_placement = true
 * 
 * REGION CALCULATION:
 *   Region coordinates = world coordinates >> 6 (divide by 64)
 *   
 *   Examples:
 *     (3222, 3218) → Region (50, 50)    [3222/64 = 50, 3218/64 = 50]
 *     (3200, 3200) → Region (50, 50)
 *     (3264, 3200) → Region (51, 50)    [3264/64 = 51]
 *     (3199, 3200) → Region (49, 50)    [3199/64 = 49]
 * 
 * REGION CHANGE DETECTION:
 *   ┌──────────────┬──────────────┬────────────┬──────────────┐
 *   │  Old Pos     │  New Pos     │  Change?   │  Action      │
 *   ├──────────────┼──────────────┼────────────┼──────────────┤
 *   │ (3200,3200)  │ (3201,3200)  │  No        │  None        │
 *   │ R(50,50)     │ R(50,50)     │            │              │
 *   ├──────────────┼──────────────┼────────────┼──────────────┤
 *   │ (3263,3200)  │ (3264,3200)  │  Yes       │  Send map    │
 *   │ R(50,50)     │ R(51,50)     │            │  Region 51,50│
 *   └──────────────┴──────────────┴────────────┴──────────────┘
 * 
 * WHY REGION CHANGE MATTERS:
 *   Game world is divided into 64×64 tile regions (chunks)
 *   Each region contains:
 *     - Terrain data (tile heights, textures)
 *     - Object data (trees, buildings, doors)
 *     - NPC spawn points
 *   
 *   When player enters new region:
 *     - Send region data to client (map tiles, objects)
 *     - Load new NPCs/objects in server
 *     - Unload old region if too far away
 * 
 * needs_placement FLAG:
 *   Always set to true when position changes via this function
 *   Indicates player needs full position update (not delta)
 *   
 *   Normal movement: Send "walk 1 tile east" (delta)
 *   Teleport:        Send "position is (3200,3200)" (absolute)
 *   
 *   needs_placement forces absolute update
 * 
 * USAGE - TELEPORT SPELL:
 *   void spell_lumbridge_teleport(Player* player) {
 *     // Cancel current movement
 *     movement_reset(&player->movement);
 *     
 *     // Teleport to Lumbridge
 *     player_set_position(player, 3222, 3218, 0);
 *     
 *     // Play teleport animation
 *     player->teleporting = true;
 *     player->update_flags |= UPDATE_FLAG_GRAPHICS;  // Teleport gfx
 *   }
 * 
 * USAGE - DEATH RESPAWN:
 *   void respawn_player(Player* player) {
 *     // Reset combat state
 *     reset_combat(player);
 *     
 *     // Teleport to respawn point
 *     player_set_position(player, 3222, 3218, 0);
 *     
 *     // Reset appearance (remove skulls, etc.)
 *     player->update_flags |= UPDATE_FLAG_APPEARANCE;
 *   }
 * 
 * COMPLEXITY: O(1) time
 */
void player_set_position(Player* player, u32 x, u32 z, u32 height) {
    u32 old_mapsquare_x = position_get_mapsquare_x(&player->position);
    u32 old_mapsquare_z = position_get_mapsquare_z(&player->position);
    
    position_init(&player->position, x, z, height);
    
    u32 new_mapsquare_x = position_get_mapsquare_x(&player->position);
    u32 new_mapsquare_z = position_get_mapsquare_z(&player->position);
    
    if (old_mapsquare_x != new_mapsquare_x || old_mapsquare_z != new_mapsquare_z) {
        player->region_changed = true;
    }
    
    player->needs_placement = true;
}

/*******************************************************************************
 * MOVEMENT PROCESSING
 ******************************************************************************/

/*
 * player_process_movement - Process queued waypoints and update position
 * 
 * @param player  Pointer to Player
 * 
 * ALGORITHM:
 *   1. Reset movement directions:
 *        primary_direction = -1
 *        secondary_direction = -1
 *   2. Check if moving:
 *        if (!movement_is_moving(&player->movement)) return
 *   3. Store old region coordinates (for change detection)
 *   4. WALK STEP:
 *        a. Dequeue waypoint: walk_point = movement_get_next(&player->movement)
 *        b. If walk_point exists:
 *             - Print debug: "walk_point x=..., z=..., dir=..."
 *             - If direction valid (>= 0):
 *                 * Move position: position_move(&position, delta_x, delta_z)
 *                 * Set primary_direction = walk_point->direction
 *             - Else: Print warning (direction = -1, shouldn't happen)
 *   5. RUN STEP (if running):
 *        a. Check if running and queue not empty
 *        b. Dequeue waypoint: run_point = movement_get_next(&player->movement)
 *        c. If run_point exists and direction valid:
 *             - Move position: position_move(&position, delta_x, delta_z)
 *             - Set secondary_direction = run_point->direction
 *   6. Check region change:
 *        a. Get new region coordinates
 *        b. If changed from old region:
 *             - Set region_changed = true
 *             - Print debug: "Player region changed from (...) to (...)"
 *             - Send new map data: map_send_load_area(player, new_x, new_y)
 *   7. Free waypoint memory:
 *        if (walk_point) free(walk_point)
 *        if (run_point) free(run_point)
 * 
 * MOVEMENT MECHANICS:
 * 
 *   WALKING (1 tile per tick):
 *     Queue: [Point{3201,3200,EAST}]
 *     
 *     Tick N:
 *       walk_point = {3201, 3200, EAST}
 *       position_move(+1, 0)  → position = (3201, 3200)
 *       primary_direction = EAST (4)
 *       secondary_direction = -1 (not running)
 * 
 *   RUNNING (2 tiles per tick):
 *     Queue: [Point{3201,3200,EAST}, Point{3202,3200,EAST}]
 *     running = true
 *     
 *     Tick N:
 *       walk_point = {3201, 3200, EAST}
 *       position = (3201, 3200)
 *       primary_direction = EAST (4)
 *       
 *       run_point = {3202, 3200, EAST}
 *       position = (3202, 3200)
 *       secondary_direction = EAST (4)
 * 
 * DIRECTION DELTA ARRAYS:
 *   DIRECTION_DELTA_X[] and DIRECTION_DELTA_Z[] map direction index to (dx,dz):
 *   
 *   Index  Direction   Delta_X  Delta_Z
 *     0    NORTHWEST     -1       +1
 *     1    NORTH          0       +1
 *     2    NORTHEAST     +1       +1
 *     3    WEST          -1        0
 *     4    EAST          +1        0
 *     5    SOUTHWEST     -1       -1
 *     6    SOUTH          0       -1
 *     7    SOUTHEAST     +1       -1
 * 
 * REGION CHANGE HANDLING:
 *   If player crosses region boundary during movement:
 *   
 *   Example: Walking east from (3263, 3200) to (3264, 3200)
 *     Old region: (50, 50)  [3263 >> 6 = 50]
 *     New region: (51, 50)  [3264 >> 6 = 51]
 *     
 *     Actions:
 *       1. Set region_changed = true
 *       2. Print: "Player region changed from (50,50) to (51,50)"
 *       3. Call map_send_load_area(player, 51, 50)
 *          → Sends new region map data to client
 * 
 * DEBUG OUTPUT:
 *   Walk step:
 *     "DEBUG: walk_point x=3201, z=3200, dir=4"
 *   
 *   Invalid direction:
 *     "DEBUG: walk_point has no direction (-1), player at (3200,3200)"
 *   
 *   Region change:
 *     "DEBUG: Player region changed from (50,50) to (51,50)"
 * 
 * MEMORY MANAGEMENT:
 *   movement_get_next() returns malloc'd Point*
 *   Must free both walk_point and run_point at end of function
 *   NULL checks prevent double-free:
 *     if (walk_point) free(walk_point);  // Safe even if NULL
 * 
 * WHEN CALLED:
 *   Every game tick (600ms) in main server loop:
 *   
 *   void game_tick() {
 *     for (u32 i = 0; i < MAX_PLAYERS; i++) {
 *       if (player_is_active(&players[i])) {
 *         player_process_movement(&players[i]);
 *       }
 *     }
 *     // Send player updates...
 *   }
 * 
 * PRIMARY vs SECONDARY DIRECTION:
 *   These are sent in player update packet:
 *   
 *   Packet structure (simplified):
 *     u8 primary_dir;    // 0-7 or 0xFF (none)
 *     u8 secondary_dir;  // 0-7 or 0xFF (none)
 *   
 *   Walking: primary set, secondary = 0xFF
 *   Running: both set
 *   Standing: both = 0xFF
 * 
 * COMPLEXITY: O(n) time where n = movement.waypoint_count
 */
void player_process_movement(Player* player) {
    player->primary_direction = -1;
    player->secondary_direction = -1;
    
    if (!movement_is_moving(&player->movement)) {
        return;
    }
    
    u32 old_x = player->position.x;
    u32 old_z = player->position.z;
    
    i32 walk_dir = movement_get_next_direction(&player->movement, player->position.x, player->position.z);
    
    if (walk_dir != -1) {
        position_move(&player->position, 
                     DIRECTION_DELTA_X[walk_dir],
                     DIRECTION_DELTA_Z[walk_dir]);
        player->primary_direction = walk_dir;
    }
    
    if (player->movement.running && movement_is_moving(&player->movement)) {
        i32 run_dir = movement_get_next_direction(&player->movement, player->position.x, player->position.z);
        if (run_dir != -1) {
            position_move(&player->position,
                         DIRECTION_DELTA_X[run_dir],
                         DIRECTION_DELTA_Z[run_dir]);
            player->secondary_direction = run_dir;
        }
    }
    
    /* Check if player needs rebuild (moved too far from origin) */
    /* TypeScript uses reload bounds: originX ± (4-5 zones) = ± 32-40 tiles */
    /* We use 32 tiles as the threshold for consistency */
    i32 origin_zone_x = player->origin_x >> 3;
    i32 origin_zone_z = player->origin_z >> 3;
    i32 current_zone_x = player->position.x >> 3;
    i32 current_zone_z = player->position.z >> 3;
    
    i32 reload_left_x = (origin_zone_x - 4) << 3;   /* 32 tiles left */
    i32 reload_right_x = (origin_zone_x + 5) << 3;  /* 40 tiles right */
    i32 reload_bottom_z = (origin_zone_z - 4) << 3; /* 32 tiles down */
    i32 reload_top_z = (origin_zone_z + 5) << 3;    /* 40 tiles up */
    
    if (player->position.x < reload_left_x || 
        player->position.x >= reload_right_x ||
        player->position.z < reload_bottom_z ||
        player->position.z >= reload_top_z) {
        
        player->region_changed = true;
        printf("DEBUG: Player moved outside reload bounds, rebuilding area\n");
        printf("  Old origin: (%u, %u), New position: (%u, %u)\n",
               player->origin_x, player->origin_z, player->position.x, player->position.z);
        
        u32 new_mapsquare_x = position_get_mapsquare_x(&player->position);
        u32 new_mapsquare_z = position_get_mapsquare_z(&player->position);
        map_send_load_area(player, new_mapsquare_x, new_mapsquare_z);
    }
}

/*******************************************************************************
 * QUERY FUNCTIONS
 ******************************************************************************/

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
 * STATE CHECK:
 *   Returns true only for PLAYER_STATE_LOGGED_IN
 *   
 *   States and return values:
 *     DISCONNECTED → false
 *     CONNECTED    → false (not yet in game)
 *     LOGGING_IN   → false (still authenticating)
 *     LOGGED_IN    → true  (actively playing)
 * 
 * WHY FUNCTION INSTEAD OF DIRECT COMPARISON?
 *   1. Abstraction:
 *        If we add more states or conditions, only change this function
 *        All callers automatically benefit from updated logic
 *   
 *   2. Readability:
 *        if (player_is_active(&player))  ← Clear intent
 *        vs
 *        if (player.state == PLAYER_STATE_LOGGED_IN)  ← More verbose
 *   
 *   3. Future extension:
 *        Could check additional conditions:
 *          return player->state == LOGGED_IN && !player->frozen;
 * 
 * COMPLEXITY: O(1) time
 */
bool player_is_active(const Player* player) {
    return player->state == PLAYER_STATE_LOGGED_IN;
}
