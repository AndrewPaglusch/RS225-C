/*******************************************************************************
 * MOVEMENT.C - Player Movement Queue Implementation (Packed Coordinates)
 *******************************************************************************
 * 
 * This file implements a FIFO queue for managing player movement waypoints
 * using packed coordinate storage (matches TypeScript Server-main).
 * 
 * KEY ALGORITHMS IMPLEMENTED:
 *   1. Coordinate packing/unpacking (32-bit integer compression)
 *   2. Queue operations (enqueue, dequeue with shift-left)
 *   3. Bounds validation (world coordinate limits)
 *   4. Dynamic direction calculation (from current position)
 *   5. Recursive waypoint validation (TypeScript validateAndAdvanceStep)
 *   6. Resource management (run energy depletion)
 * 
 * EDUCATIONAL CONCEPTS:
 *   - Bit manipulation for data compression
 *   - Array-based queue (shift-on-dequeue approach)
 *   - Stack allocation (no heap/malloc needed)
 *   - Bounds checking and input validation
 *   - Game state management (tick-based updates)
 *   - Cache-friendly contiguous memory layout
 * 
 * PACKED COORDINATE ARCHITECTURE (matches TypeScript):
 * 
 *   TypeScript Reference:
 *     waypoints: Int32Array = new Int32Array(25);
 *     waypoints[i] = CoordGrid.packCoord(level, x, z);
 * 
 *   C Implementation:
 *     i32 waypoints[MAX_WAYPOINTS];
 *     waypoints[i] = coord_pack(level, x, z);
 * 
 *   32-bit packing: (z & 0x3fff) | ((x & 0x3fff) << 14) | ((level & 0x3) << 28)
 * 
 * QUEUE IMPLEMENTATION CHOICE:
 * 
 * This uses a simple shift-left approach rather than circular buffer:
 * 
 *   SHIFT-LEFT (used here):
 *     Enqueue: O(1) - append to end
 *     Dequeue: O(n) - shift all elements left
 *     Space:   O(1) - no wasted slots, inline storage
 * 
 *   CIRCULAR BUFFER (alternative):
 *     Enqueue: O(1) - write to tail, increment tail
 *     Dequeue: O(1) - read from head, increment head
 *     Space:   O(n) - wastes slots until wrap-around
 * 
 * WHY SHIFT-LEFT FOR THIS USE CASE?
 *   - MAX_WAYPOINTS is small (25 typical)
 *   - O(n) shift takes ~50-100 CPU cycles (negligible)
 *   - Simpler code (no head/tail pointer management)
 *   - No modulo arithmetic (faster)
 *   - No wasted array capacity
 *   - Dequeue happens once per game tick (600ms) so O(n) is acceptable
 *   - Matches TypeScript behavior (array shift)
 * 
 * PERFORMANCE CALCULATION:
 *   Assume 25 waypoints, modern CPU at 3GHz:
 *     Shift operation: ~2 CPU cycles per i32 (memmove)
 *     Total: 25 × 2 = 50 cycles = 0.000017ms (17 nanoseconds)
 *     Game tick: 600ms = 600,000,000 nanoseconds
 *     Shift cost: 0.000003% of tick time (completely negligible)
 * 
 ******************************************************************************/

#include "movement.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*******************************************************************************
 * COORDINATE PACKING/UNPACKING
 ******************************************************************************/

/*
 * coord_pack - Pack 3D coordinates into 32-bit integer
 * 
 * @param level  Plane level (0-3)
 * @param x      X coordinate (0-16383)
 * @param z      Z coordinate (0-16383)
 * @return       Packed 32-bit coordinate
 * 
 * ALGORITHM (matches TypeScript CoordGrid.packCoord):
 *   result = (z & 0x3fff) | ((x & 0x3fff) << 14) | ((level & 0x3) << 28)
 * 
 * BIT LAYOUT:
 *   ┌─────────┬─────────┬─────────┬─────────┐
 *   │  31-30  │  29-28  │  27-14  │  13-0   │
 *   ├─────────┼─────────┼─────────┼─────────┤
 *   │ unused  │  level  │    x    │    z    │
 *   │ 2 bits  │ 2 bits  │ 14 bits │ 14 bits │
 *   └─────────┴─────────┴─────────┴─────────┘
 * 
 * EXAMPLE:
 *   coord_pack(0, 3222, 3218)
 *   
 *   Binary breakdown:
 *     z     = 3218 = 0b00110010010010
 *     x     = 3222 = 0b00110010010110
 *     level = 0    = 0b00
 *   
 *   Packed:
 *     Bits  0-13: 0b00110010010010  (z = 3218)
 *     Bits 14-27: 0b00110010010110  (x = 3222)
 *     Bits 28-29: 0b00              (level = 0)
 *     Bits 30-31: 0b00              (unused)
 *   
 *   Result: 0x0032C832
 * 
 * COMPLEXITY: O(1) time - bitwise operations
 */
u32 coord_pack(u32 level, u32 x, u32 z) {
    return (z & 0x3fff) | ((x & 0x3fff) << 14) | ((level & 0x3) << 28);
}

/*
 * coord_unpack - Unpack 32-bit integer into 3D coordinates
 * 
 * @param packed  Packed coordinate value
 * @param level   Output pointer for plane level
 * @param x       Output pointer for X coordinate
 * @param z       Output pointer for Z coordinate
 * 
 * ALGORITHM (matches TypeScript CoordGrid.unpackCoord):
 *   z     = packed & 0x3fff
 *   x     = (packed >> 14) & 0x3fff
 *   level = (packed >> 28) & 0x3
 * 
 * EXAMPLE:
 *   u32 level, x, z;
 *   coord_unpack(0x0032C832, &level, &x, &z);
 *   
 *   Extraction:
 *     z     = 0x0032C832 & 0x3fff        = 0xC32  = 3218
 *     x     = (0x0032C832 >> 14) & 0x3fff = 0xC96  = 3222
 *     level = (0x0032C832 >> 28) & 0x3    = 0x0    = 0
 *   
 *   Result: level=0, x=3222, z=3218
 * 
 * COMPLEXITY: O(1) time - bitwise operations
 */
void coord_unpack(u32 packed, u32* level, u32* x, u32* z) {
    *z = packed & 0x3fff;
    *x = (packed >> 14) & 0x3fff;
    *level = (packed >> 28) & 0x3;
}

/*******************************************************************************
 * PATHFINDING
 ******************************************************************************/

/*
 * movement_naive_path - Calculate naive line path from src to dest
 * 
 * @param handler  Pointer to MovementHandler to fill with waypoints
 * @param src_x    Starting X coordinate
 * @param src_z    Starting Z coordinate  
 * @param dest_x   Destination X coordinate
 * @param dest_z   Destination Z coordinate
 * 
 * ALGORITHM (Naive Line Pathfinding):
 *   1. Calculate deltas: dx = dest_x - src_x, dz = dest_z - src_z
 *   2. Walk diagonally while both dx and dz are non-zero
 *      - Each diagonal step: x += sign(dx), z += sign(dz)
 *   3. Walk straight (horizontal or vertical) for remaining distance
 *      - If dx remains: walk horizontally
 *      - If dz remains: walk vertically
 * 
 * EXAMPLE TRACE:
 *   From (3200, 3200) to (3205, 3203):
 *   
 *   Initial: dx = +5, dz = +3
 *   
 *   Diagonal phase (while dx!=0 && dz!=0):
 *     Step 1: (3201, 3201)  dx=4, dz=2
 *     Step 2: (3202, 3202)  dx=3, dz=1
 *     Step 3: (3203, 3203)  dx=2, dz=0
 *   
 *   Straight phase (dx!=0, dz==0):
 *     Step 4: (3204, 3203)  dx=1, dz=0
 *     Step 5: (3205, 3203)  dx=0, dz=0  DONE
 * 
 * VISUALIZATION:
 *   
 *   3203 │ . . . 3─4─5
 *   3202 │ . . 2 . . .
 *   3201 │ . 1 . . . .
 *   3200 │ S . . . . .
 *        └───────────────
 *          3200  3202  3204
 * 
 * LIMITATIONS:
 *   - No collision detection (walks through walls!)
 *   - Not optimal path (may not be shortest around obstacles)
 *   - Use proper A* pathfinding for production
 * 
 * COMPLEXITY:
 *   Time: O(N) where N = max(|dx|, |dz|)
 *   Space: O(1) - uses inline waypoint array
 */
void movement_naive_path(MovementHandler* handler, u32 src_x, u32 src_z, u32 dest_x, u32 dest_z) {
    i32 dx = (i32)dest_x - (i32)src_x;
    i32 dz = (i32)dest_z - (i32)src_z;
    
    u32 current_x = src_x;
    u32 current_z = src_z;
    
    /* Walk diagonally while both dx and dz are non-zero */
    while (dx != 0 && dz != 0 && handler->waypoint_index < MAX_WAYPOINTS - 1) {
        if (dx > 0) {
            current_x++;
            dx--;
        } else {
            current_x--;
            dx++;
        }
        
        if (dz > 0) {
            current_z++;
            dz--;
        } else {
            current_z--;
            dz++;
        }
        
        movement_add_step(handler, current_x, current_z);
    }
    
    /* Walk horizontally for remaining X distance */
    while (dx != 0 && handler->waypoint_index < MAX_WAYPOINTS - 1) {
        if (dx > 0) {
            current_x++;
            dx--;
        } else {
            current_x--;
            dx++;
        }
        
        movement_add_step(handler, current_x, current_z);
    }
    
    /* Walk vertically for remaining Z distance */
    while (dz != 0 && handler->waypoint_index < MAX_WAYPOINTS - 1) {
        if (dz > 0) {
            current_z++;
            dz--;
        } else {
            current_z--;
            dz++;
        }
        
        movement_add_step(handler, current_x, current_z);
    }
}

/*******************************************************************************
 * INITIALIZATION AND CLEANUP
 ******************************************************************************/

/*
 * movement_init - Initialize movement handler
 * 
 * @param handler  Pointer to MovementHandler to initialize
 * 
 * ALGORITHM:
 *   1. Zero entire struct: memset(handler, 0, sizeof(MovementHandler))
 *   2. Set waypoint_index = -1 (empty queue, matches TypeScript)
 *   3. Set initial run energy: run_energy = 10000
 * 
 * ZEROED FIELDS (via memset):
 *   - All waypoint values: 0
 *   - waypoint_index: 0 (overwritten to -1 below)
 *   - run_path: false (0)
 *   - running: false (0)
 *   - run_energy: 0 (overwritten to 10000 below)
 * 
 * WHY memset INSTEAD OF FIELD-BY-FIELD?
 *   - Faster (single CPU instruction on most architectures)
 *   - Safer (guaranteed to zero padding bytes)
 *   - More maintainable (adding new fields doesn't require init updates)
 * 
 * RUN ENERGY INITIAL VALUE:
 *   10000 = 100.00% (displayed to player as "100")
 *   Chosen for precision: allows 0.01% granularity
 *   Range: [0, 10000] mapped to display [0, 100]
 * 
 * EXAMPLE:
 *   MovementHandler handler;  // Uninitialized (garbage values)
 *   movement_init(&handler);  // Now safe to use
 *   
 *   After init:
 *   ┌────────────────────────────────────────┐
 *   │ waypoints[0..MAX_WAYPOINTS-1] = 0      │
 *   │ waypoint_index = -1 (empty)            │
 *   │ run_path       = false                 │
 *   │ running        = false                 │
 *   │ run_energy     = 10000                 │
 *   └────────────────────────────────────────┘
 * 
 * COMPLEXITY: O(1) time - memset is constant time for fixed struct size
 */
void movement_init(MovementHandler* handler) {
    memset(handler, 0, sizeof(MovementHandler));
    handler->waypoint_index = -1;
    handler->run_energy = 10000;
}

/*
 * movement_destroy - Clear movement queue
 * 
 * @param handler  Pointer to MovementHandler to clean up
 * 
 * ALGORITHM:
 *   1. Set waypoint_index = -1 (mark queue as empty)
 * 
 * MEMORY MANAGEMENT:
 *   No heap allocation used - waypoints stored inline in struct
 *   No free() calls needed (unlike old Point* approach)
 *   Much simpler than old O(n) loop with multiple free() calls
 * 
 * IDEMPOTENT OPERATION:
 *   Safe to call multiple times:
 *     First call:  Sets waypoint_index = -1
 *     Second call: Sets waypoint_index = -1 again (no-op)
 * 
 * EXAMPLE - LIFECYCLE:
 *   movement_add_step(&handler, 3200, 3200);
 *     → waypoints[0] = coord_pack(0, 3200, 3200)
 *     → waypoint_index = 0
 *   
 *   movement_add_step(&handler, 3201, 3201);
 *     → waypoints[1] = coord_pack(0, 3201, 3201)
 *     → waypoint_index = 1
 *   
 *   movement_destroy(&handler);
 *     → waypoint_index = -1
 *     → Queue now empty (waypoint data still in array but ignored)
 * 
 * WHEN TO CALL:
 *   - Player logout (optional - handler will be destroyed anyway)
 *   - Player teleport (via movement_reset which calls this)
 *   - Server shutdown (optional)
 * 
 * COMPLEXITY: O(1) time - single assignment
 */
void movement_destroy(MovementHandler* handler) {
    handler->waypoint_index = -1;
}

/*
 * movement_reset - Clear queue and reset movement state
 * 
 * @param handler  Pointer to MovementHandler to reset
 * 
 * ALGORITHM:
 *   1. Call movement_destroy(handler) to free waypoints
 *   2. Set run_path = false
 *   3. Set running  = false
 * 
 * NOTE: Does NOT reset run_energy
 *   Energy persists across movement resets
 *   Only regenerates over time (not implemented here)
 * 
 * STATE AFTER RESET:
 *   - waypoint_count = 0  (via movement_destroy)
 *   - run_path       = false
 *   - running        = false
 *   - run_energy     = UNCHANGED
 * 
 * USE CASES:
 *   1. TELEPORT:
 *        movement_reset(&player->movement);
 *        player->position = teleport_dest;
 *        // Queued path is now invalid, discard it
 *   
 *   2. COMBAT INTERRUPTION:
 *        if (player_attacked(&player)) {
 *          movement_reset(&player->movement);
 *          // Force stop (real RS stops movement on combat)
 *        }
 *   
 *   3. CLICK NEW DESTINATION (cancel current path):
 *        movement_reset(&player->movement);
 *        calculate_path(current_pos, new_dest, &player->movement);
 * 
 * COMPLEXITY: O(1) time (no heap to free)
 */
void movement_reset(MovementHandler* handler) {
    movement_destroy(handler);
    handler->run_path = false;
    handler->running = false;
}

/*******************************************************************************
 * QUEUE OPERATIONS
 ******************************************************************************/

/*
 * movement_add_step - Add waypoint to movement queue (packed coordinates)
 * 
 * @param handler  Pointer to MovementHandler
 * @param x        World X coordinate of waypoint
 * @param z        World Z coordinate of waypoint
 * 
 * ALGORITHM (simplified from old Point* approach):
 *   1. Capacity check: if (waypoint_index >= MAX_WAYPOINTS-1) → reject
 *   2. Bounds check: if (x > 12800 || z > 12800) → reject with warning
 *   3. Pack coordinates: coord_pack(0, x, z)
 *   4. Store: waypoints[++waypoint_index] = packed_coord
 * 
 * NOTE: Direction NOT calculated here (matches TypeScript)
 *   Old approach: Stored direction in Point struct
 *   New approach: Direction calculated dynamically on dequeue
 *   
 *   Why this is better:
 *     - Same waypoint can be approached from different positions
 *     - Simpler enqueue (no direction calculation)
 *     - Matches TypeScript PathingEntity behavior
 *     - Smaller memory footprint (4 bytes vs 12-16 bytes)
 * 
 * BOUNDS VALIDATION:
 *   RuneScape world coordinate system:
 *   ┌────────────────────────────────────────────────┐
 *   │ Valid range: 0 ≤ x ≤ 12800, 0 ≤ z ≤ 12800      │
 *   │ Total tiles: 12800 × 12800 = 163,840,000       │
 *   ├────────────────────────────────────────────────┤
 *   │ Main playable area: ~3100-3500 (Lumbridge)     │
 *   │ Wilderness: ~3000-3900                         │
 *   │ Edges: Often water/unreachable (0-1000)        │
 *   └────────────────────────────────────────────────┘
 * 
 *   Why 12800?
 *     - World is divided into 200×200 regions
 *     - Each region is 64×64 tiles
 *     - Total: 200×64 = 12800 tiles per axis
 * 
 * PACKED COORDINATE STORAGE:
 *   Direction NOT stored in waypoint (calculated on dequeue)
 * 
 *   Example path: (3200,3200) → (3201,3200) → (3201,3201)
 *   
 *   Step 1: movement_add_step(&handler, 3201, 3200)
 *     → coord_pack(0, 3201, 3200) = 0x0032C800
 *     → waypoints[0] = 0x0032C800
 *     → waypoint_index = 0
 *   
 *   Step 2: movement_add_step(&handler, 3201, 3201)
 *     → coord_pack(0, 3201, 3201) = 0x0032C801
 *     → waypoints[1] = 0x0032C801
 *     → waypoint_index = 1
 *   
 *   Queue after adding all steps:
 *   ┌─────────────────────────────────────────────────────────┐
 *   │ waypoints[0] = 0x0032C800  (x=3201, z=3200)             │
 *   │ waypoints[1] = 0x0032C801  (x=3201, z=3201)             │
 *   │ waypoint_index = 1                                      │
 *   └─────────────────────────────────────────────────────────┘
 *   
 *   Direction calculated later in movement_get_next_direction():
 *     - Unpacks waypoint
 *     - Compares to current player position
 *     - Calculates direction dynamically
 * 
 * REJECTION SCENARIOS:
 * 
 *   1. Queue full:
 *        for (int i = 0; i < MAX_WAYPOINTS + 10; i++) {
 *          movement_add_step(&handler, 3200+i, 3200);
 *        }
 *        → First MAX_WAYPOINTS accepted, rest silently rejected
 *   
 *   2. Out of bounds:
 *        movement_add_step(&handler, 99999, 99999);
 *        → Prints: "WARNING: movement_add_step out of bounds: x=99999, z=99999"
 *        → Returns immediately (no waypoint added)
 * 
 * MEMORY EFFICIENCY:
 *   No heap allocation needed (inline array storage):
 *     Each waypoint: 4 bytes (packed i32)
 *     Max memory: MAX_WAYPOINTS × 4 = 25 × 4 = 100 bytes per player
 *   
 *   Old Point* approach:
 *     Each waypoint: 12-16 bytes (x, z, direction + padding)
 *     Max memory: 25 × 16 = 400 bytes per player
 *   
 *   Improvement:
 *     4x less memory usage
 *     Better cache locality (contiguous array)
 *     No heap fragmentation
 *     No malloc/free overhead
 *   
 *   With 2000 players:
 *     Old: 2000 × 400 = 800 KB
 *     New: 2000 × 100 = 200 KB
 *     Savings: 600 KB (75% reduction)
 * 
 * NO DIRECTION VALIDATION ON ENQUEUE:
 *   Client path is trusted (validation happens on dequeue)
 *   Allows any waypoint sequence (adjacent or not)
 *   Direction calculated dynamically based on current player position
 *   
 *   This matches TypeScript PathingEntity behavior:
 *     - queueWaypoint() just packs and stores
 *     - validateAndAdvanceStep() handles direction calculation
 * 
 * COMPLEXITY: O(1) time, O(1) space (no allocation)
 */
void movement_add_step(MovementHandler* handler, u32 x, u32 z) {
    if (handler->waypoint_index >= MAX_WAYPOINTS - 1) {
        return;
    }
    
    if (x > 12800 || z > 12800) {
        printf("WARNING: movement_add_step out of bounds: x=%u, z=%u\n", x, z);
        return;
    }
    
    handler->waypoints[++handler->waypoint_index] = coord_pack(0, x, z);
}

/*
 * movement_set_run_path - Set run toggle state
 * 
 * @param handler  Pointer to MovementHandler
 * @param running  true = attempt to run, false = walk
 * 
 * ALGORITHM:
 *   1. Set run_path = running (store client's toggle state)
 *   2. Set actual running state:
 *        handler->running = (running && handler->run_energy > 0)
 * 
 * RUNNING STATE LOGIC:
 *   Player can only run if BOTH conditions are met:
 *     1. run_path == true  (client toggled run ON)
 *     2. run_energy > 0    (player has energy remaining)
 *   
 *   Truth table:
 *   ┌───────────┬──────────────┬────────────────┐
 *   │ run_path  │ run_energy   │handler->running│
 *   ├───────────┼──────────────┼────────────────┤
 *   │  false    │   10000      │  false         │  ← Walk (toggle off)
 *   │  false    │   0          │  false         │  ← Walk (toggle off)
 *   │  true     │   5000       │  true          │  ← Run (has energy)
 *   │  true     │   0          │  false         │  ← Walk (no energy)
 *   └───────────┴──────────────┴────────────────┘
 * 
 * ENERGY DEPLETION SCENARIO:
 *   1. Client toggles run ON:
 *        movement_set_run_path(&handler, true);
 *        → run_path = true, running = true (energy = 10000)
 *   
 *   2. Player runs 10000 tiles:
 *        Each movement_get_next() decreases energy by 1
 *        run_energy: 10000 → 9999 → ... → 1 → 0
 *   
 *   3. Next movement_set_run_path call:
 *        movement_set_run_path(&handler, true);
 *        → run_path = true, running = false (energy = 0)
 *        → Player walks even though toggle is ON
 *   
 *   4. Energy regenerates (not in this file):
 *        run_energy += regeneration_rate;  // In game tick loop
 *   
 *   5. Next movement_set_run_path call:
 *        movement_set_run_path(&handler, true);
 *        → run_path = true, running = true (energy > 0)
 *        → Player can run again
 * 
 * CLIENT-SERVER SYNCHRONIZATION:
 *   Client packet opcodes:
 *     WALK_OPCODE = 248  (example, actual value may differ)
 *     RUN_OPCODE  = 164
 *   
 *   Server packet handler:
 *     u8 opcode = buffer_read_u8(&packet);
 *     bool is_run = (opcode == RUN_OPCODE);
 *     movement_set_run_path(&player->movement, is_run);
 *     
 *     while (buffer_available(&packet)) {
 *       u32 x = buffer_read_u16(&packet);
 *       u32 z = buffer_read_u16(&packet);
 *       movement_add_step(&player->movement, x, z);
 *     }
 * 
 * WHY SEPARATE run_path AND running?
 *   - run_path: Persistent client preference (survives energy depletion)
 *   - running:  Actual state (considers energy availability)
 *   
 *   Without separation:
 *     Player runs out of energy → forced to walk
 *     Client thinks run is ON but server walks → desync
 *   
 *   With separation:
 *     run_path stays true (client state preserved)
 *     running becomes false (server enforces energy)
 *     When energy regenerates, running automatically becomes true
 * 
 * COMPLEXITY: O(1) time
 */
void movement_set_run_path(MovementHandler* handler, bool running) {
    handler->run_path = running;
    handler->running = running && handler->run_energy > 0;
}

/*
 * movement_finish - Finalize movement path
 * 
 * @param handler  Pointer to MovementHandler
 * 
 * CURRENT IMPLEMENTATION:
 *   Empty function (no-op)
 * 
 * POTENTIAL FUTURE USE:
 *   1. Pathfinding completion marker:
 *        Could set a flag indicating "path is complete, no more waypoints coming"
 *        Useful for distinguishing partial paths from complete paths
 *   
 *   2. Statistics tracking:
 *        Calculate total path distance for analytics:
 *          u32 total_distance = 0;
 *          for (u32 i = 1; i < waypoint_count; i++) {
 *            total_distance += distance(waypoints[i-1], waypoints[i]);
 *          }
 *   
 *   3. Path optimization:
 *        Remove redundant waypoints (e.g., three collinear points → two points)
 *   
 *   4. Animation triggers:
 *        Queue arrival animation/sound when path completes
 * 
 * WHY PLACEHOLDER FUNCTION?
 *   - Common in game dev to add API hooks before implementation
 *   - Easier to add functionality later without API changes
 *   - Documents intended future behavior
 * 
 * COMPLEXITY: O(1) time (currently no-op)
 */
void movement_finish(MovementHandler* handler) {
}

/*
 * movement_get_next_direction - Calculate next movement direction with recursive validation
 * 
 * @param handler    Pointer to MovementHandler
 * @param current_x  Player's current X coordinate
 * @param current_z  Player's current Z coordinate
 * @return           Direction (0-7) or -1 if no valid movement
 * 
 * ALGORITHM (FIXED - multi-tile waypoint support):
 *   1. If queue empty (waypoint_index == -1), return -1
 *   2. Unpack first waypoint: coord_unpack(waypoints[0], &level, &x, &z)
 *   3. Calculate deltas: dx = x - current_x, dz = z - current_z
 *   4. NORMALIZE deltas to single-tile step: step_dx/step_dz = -1, 0, or +1
 *   5. Calculate direction: position_direction(step_dx, step_dz)
 *   6. If direction == -1 (already at waypoint):
 *        a. Dequeue waypoint (shift array left, decrement index)
 *        b. Recursively call self (check next waypoint)
 *   7. Else (valid direction found):
 *        a. Calculate next position after move
 *        b. If next position == waypoint, THEN dequeue waypoint
 *        c. Decrease run energy if running
 *        d. Return direction
 * 
 * FIX:
 *   Waypoints are NOT dequeued until player reaches them. Previous version
 *   dequeued immediately, causing players to stop short of destination.
 *   Now waypoints persist until player position matches waypoint coordinates.
 * 
 * RECURSIVE VALIDATION (matches TypeScript):
 *   TypeScript Reference:
 *     private validateAndAdvanceStep(): number {
 *       const dir: number | null = this.takeStep();
 *       if (dir === null) return -1;
 *       if (dir === -1) {
 *         this.waypointIndex--;
 *         if (this.waypointIndex != -1) {
 *           return this.validateAndAdvanceStep();  // RECURSIVE
 *         }
 *         return -1;
 *       }
 *       this.x = CoordGrid.moveX(this.x, dir);
 *       this.z = CoordGrid.moveZ(this.z, dir);
 *       return dir;
 *     }
 * 
 *   C Implementation:
 *     Same recursive logic - when player already at waypoint, dequeue and
 *     check next waypoint without moving. Continues until valid move found.
 * 
 * EXAMPLE - MULTI-TILE WAYPOINT:
 * 
 *   Initial state:
 *   ┌──────────────────────────────────────────────────────────┐
 *   │ waypoints[0] = 0x0032C900  (x=3225, z=3200)  ← 5 tiles E │
 *   │ waypoint_index = 0                                       │
 *   │ Current player position: (3220, 3200)                    │
 *   └──────────────────────────────────────────────────────────┘
 * 
 *   TICK 1:
 *     dx = 3225 - 3220 = 5 (not normalized)
 *     dz = 0
 *     step_dx = 1, step_dz = 0  ← NORMALIZED to single tile
 *     direction = DIR_E (4)
 *     next_pos = (3221, 3200)
 *     next_pos != waypoint(3225, 3200) → DON'T dequeue
 *     return DIR_E
 *     Player moves to (3221, 3200)
 * 
 *   TICK 2:
 *     dx = 3225 - 3221 = 4
 *     step_dx = 1, step_dz = 0
 *     direction = DIR_E (4)
 *     next_pos = (3222, 3200)
 *     next_pos != waypoint → DON'T dequeue
 *     Player moves to (3222, 3200)
 * 
 *   TICK 3, 4: (same pattern, moving east)
 * 
 *   TICK 5:
 *     dx = 3225 - 3224 = 1
 *     step_dx = 1, step_dz = 0
 *     direction = DIR_E (4)
 *     next_pos = (3225, 3200)
 *     next_pos == waypoint → DEQUEUE waypoint!
 *     Player moves to (3225, 3200)  ← REACHED DESTINATION
 * 
 *   Final: Player successfully reached waypoint at (3225, 3200)
 * 
 * NO MEMORY ALLOCATION:
 *   Returns direction value (i32), not pointer
 *   No malloc/free needed by caller
 *   Much simpler than old Point* approach
 * 
 * CALLER USAGE (simple, no memory management):
 *   i32 dir = movement_get_next_direction(&player->movement,
 *                                         player->position.x,
 *                                         player->position.z);
 *   if (dir != -1) {
 *     position_move(&player->position, 
 *                  DIRECTION_DELTA_X[dir], 
 *                  DIRECTION_DELTA_Z[dir]);
 *     player->primary_direction = dir;
 *   }
 * 
 * SHIFT OPERATION:
 *   For array of N packed i32 values:
 *     for (i = 0; i <= waypoint_index; i++)
 *       waypoints[i] = waypoints[i+1];  // Copy 4-byte i32
 *   
 *   Example timing (25 waypoints, 3GHz CPU):
 *     25 i32s × 2 cycles = 50 cycles = 0.000017ms
 *   
 *   Faster than old Point* approach:
 *     Old: 25 pointers × 2 cycles + 25 free() calls
 *     New: 25 i32s × 2 cycles (no free needed)
 * 
 * ENERGY DEPLETION:
 *   Only decreases energy when running:
 *     if (handler->running && handler->run_energy > 0)
 *       handler->run_energy -= 1;
 *   
 *   Energy reaches 0:
 *     run_energy: 10 → 9 → ... → 1 → 0
 *     Next call to movement_set_run_path() will set running = false
 *   
 *   Walking does NOT decrease energy:
 *     running == false → energy stays constant
 *     Allows energy to regenerate (if regeneration logic added)
 * 
 * EMPTY QUEUE HANDLING:
 *   if (handler->waypoint_index == -1)
 *     return -1;
 *   
 *   Caller should check for -1:
 *     i32 dir = movement_get_next_direction(&handler, x, z);
 *     if (dir == -1) {
 *       // Player has finished moving, standing still
 *     }
 * 
 * GAME TICK INTEGRATION:
 *   Every 600ms tick:
 *     for each player:
 *       if (movement_is_moving(&player->movement)) {
 *         i32 dir = movement_get_next_direction(&player->movement,
 *                                               player->position.x,
 *                                               player->position.z);
 *         if (dir != -1) {
 *           position_move(&player->position, 
 *                        DIRECTION_DELTA_X[dir], 
 *                        DIRECTION_DELTA_Z[dir]);
 *           send_position_update(player);
 *         }
 *       }
 * 
 * COMPLEXITY: O(n) time worst case (recursive), O(n) for shift (n = waypoint_index)
 */
i32 movement_get_next_direction(MovementHandler* handler, u32 current_x, u32 current_z) {
    if (handler->waypoint_index == -1) {
        return -1;
    }
    
    u32 level, x, z;
    coord_unpack(handler->waypoints[0], &level, &x, &z);
    
    i32 dx = x - current_x;
    i32 dz = z - current_z;
    
    /* Normalize deltas to -1, 0, or +1 for single-tile movement */
    i32 step_dx = (dx < 0) ? -1 : (dx > 0) ? 1 : 0;
    i32 step_dz = (dz < 0) ? -1 : (dz > 0) ? 1 : 0;
    
    i32 direction = position_direction(step_dx, step_dz);
    
    if (direction == -1) {
        /* Already at waypoint, dequeue and check next */
        handler->waypoint_index--;
        if (handler->waypoint_index != -1) {
            for (i32 i = 0; i <= handler->waypoint_index; i++) {
                handler->waypoints[i] = handler->waypoints[i + 1];
            }
            return movement_get_next_direction(handler, current_x, current_z);
        }
        return -1;
    }
    
    /* Calculate the position after this move */
    u32 next_x = current_x + step_dx;
    u32 next_z = current_z + step_dz;
    
    /* Check if this move will reach the waypoint */
    if (next_x == x && next_z == z) {
        /* We'll reach the waypoint after this move, dequeue it */
        handler->waypoint_index--;
        if (handler->waypoint_index != -1) {
            for (i32 i = 0; i <= handler->waypoint_index; i++) {
                handler->waypoints[i] = handler->waypoints[i + 1];
            }
        }
    }
    
    /* Decrease energy if running */
    if (handler->running && handler->run_energy > 0) {
        handler->run_energy -= 1;
    }
    
    return direction;
}

/*******************************************************************************
 * QUERY FUNCTIONS
 ******************************************************************************/

/*
 * movement_is_moving - Check if player has queued waypoints
 * 
 * @param handler  Pointer to MovementHandler
 * @return         true if waypoint_count > 0, false otherwise
 * 
 * USAGE:
 *   Game tick loop:
 *     if (movement_is_moving(&player->movement)) {
 *       // Player is moving, process next step
 *       Point* next = movement_get_next(&player->movement);
 *       // ...
 *     } else {
 *       // Player is stationary, skip movement update
 *     }
 * 
 * OPTIMIZATION:
 *   Checking is_moving before get_next avoids:
 *     - Unnecessary function call overhead
 *     - NULL pointer checks in caller
 *     - Clearer logic (explicit intent)
 * 
 * EQUIVALENT TO:
 *   return handler->waypoint_count > 0;
 * 
 * COMPLEXITY: O(1) time
 */
bool movement_is_moving(const MovementHandler* handler) {
    return handler->waypoint_index != -1;
}

/*
 * movement_get_waypoint_count - Get number of queued waypoints
 * 
 * @param handler  Pointer to MovementHandler
 * @return         Number of waypoints in queue [0, MAX_WAYPOINTS]
 * 
 * USAGE EXAMPLES:
 * 
 *   1. Estimate time to arrival:
 *        u32 steps = movement_get_waypoint_count(&player->movement);
 *        u32 ticks = player->movement.running ? (steps / 2) : steps;
 *        u32 seconds = ticks * 0.6;  // 600ms per tick
 *        printf("ETA: %u seconds\n", seconds);
 *   
 *   2. Progress display:
 *        u32 total_steps = 100;
 *        u32 remaining = movement_get_waypoint_count(&player->movement);
 *        u32 completed = total_steps - remaining;
 *        printf("Progress: %u/%u\n", completed, total_steps);
 *   
 *   3. Capacity check:
 *        u32 count = movement_get_waypoint_count(&handler);
 *        if (count < MAX_WAYPOINTS) {
 *          movement_add_step(&handler, x, z);  // Has space
 *        }
 * 
 * COMPLEXITY: O(1) time
 */
u32 movement_get_waypoint_count(const MovementHandler* handler) {
    return handler->waypoint_index + 1;
}

/*
 * movement_remove_first_waypoint - Remove first waypoint without returning it
 * 
 * @param handler  Pointer to MovementHandler
 * 
 * ALGORITHM:
 *   1. If queue empty (waypoint_count == 0), return (no-op)
 *   2. Free first waypoint: free(waypoints[0])
 *   3. Shift remaining waypoints left:
 *        for i in [1, waypoint_count):
 *          waypoints[i-1] = waypoints[i]
 *   4. Decrement count: waypoint_count--
 * 
 * DIFFERENCE FROM movement_get_next:
 *   ┌──────────────────────┬─────────────────────────────────┐
 *   │ movement_get_next    │ movement_remove_first_waypoint  │
 *   ├──────────────────────┼─────────────────────────────────┤
 *   │ Allocates copy       │ No allocation                   │
 *   │ Returns Point*       │ Returns void                    │
 *   │ Decreases energy     │ Does NOT decrease energy        │
 *   │ Caller must free()   │ No memory management needed     │
 *   └──────────────────────┴─────────────────────────────────┘
 * 
 * WHEN TO USE:
 *   1. Discard invalid waypoint:
 *        Point* next = movement_get_next(&handler);
 *        if (is_blocked(next->x, next->z)) {
 *          // Can't move there, skip this waypoint
 *          free(next);
 *          // Already removed from queue
 *        }
 *   
 *   2. Fast-forward through path:
 *        while (movement_get_waypoint_count(&handler) > 10) {
 *          movement_remove_first_waypoint(&handler);
 *        }
 *        // Keep only last 10 waypoints
 *   
 *   3. Cancel first step (without processing):
 *        movement_remove_first_waypoint(&handler);
 *        // Unlike get_next, doesn't decrease energy
 * 
 * COMPARISON - COLLISION HANDLING:
 *   
 *   Approach 1 (re-queue):
 *     Point* next = movement_get_next(&handler);
 *     if (is_blocked(next->x, next->z)) {
 *       movement_add_step(&handler, next->x, next->z);  // Try again next tick
 *       free(next);
 *     }
 *   
 *   Approach 2 (skip):
 *     Point* next = movement_get_next(&handler);
 *     if (is_blocked(next->x, next->z)) {
 *       free(next);  // Discard, continue to next waypoint
 *     }
 *   
 *   Approach 3 (stop):
 *     Point* next = movement_get_next(&handler);
 *     if (is_blocked(next->x, next->z)) {
 *       movement_reset(&handler);  // Cancel entire path
 *       free(next);
 *     }
 * 
 * MEMORY MANAGEMENT:
 *   Frees waypoint internally, no return value:
 *     free(handler->waypoints[0]);
 *   
 *   Caller does NOT need to free anything:
 *     movement_remove_first_waypoint(&handler);
 *     // Done, no cleanup needed
 * 
 * EMPTY QUEUE SAFETY:
 *   if (handler->waypoint_count == 0)
 *     return;  // No-op, safe to call on empty queue
 * 
 * COMPLEXITY: O(n) time where n = waypoint_count (due to shift)
 */
void movement_remove_first_waypoint(MovementHandler* handler) {
    if (handler->waypoint_index == -1) {
        return;
    }
    
    for (i32 i = 0; i <= handler->waypoint_index; i++) {
        handler->waypoints[i] = handler->waypoints[i + 1];
    }
    handler->waypoint_index--;
}
