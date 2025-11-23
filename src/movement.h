/*******************************************************************************
 * MOVEMENT.H - Player Movement Queue and Pathfinding System
 *******************************************************************************
 * 
 * EDUCATIONAL PURPOSE:
 * This file demonstrates fundamental concepts in:
 *   - Queue data structures (FIFO waypoint processing)
 *   - Pathfinding and navigation (step-by-step movement)
 *   - Game loop integration (tick-based position updates)
 *   - Resource management (run energy depletion)
 *   - Array-based queue implementation (no linked lists)
 *   - Bounds checking and validation (world coordinate limits)
 * 
 * CORE CONCEPT - MOVEMENT QUEUE:
 * 
 * In multiplayer games, player movement is broken down into discrete steps:
 *   1. Client sends path (sequence of waypoints)
 *   2. Server queues waypoints in MovementHandler
 *   3. Each game tick (600ms), server processes next waypoint
 *   4. Position update sent to all nearby players
 * 
 * WHY QUEUE MOVEMENT?
 *   - Smooth animation (prevents teleporting)
 *   - Cheat prevention (validates each step server-side)
 *   - Network efficiency (send path once, not continuous updates)
 *   - Synchronization (all players see same movement)
 * 
 * MOVEMENT MECHANICS:
 * ┌──────────────────────────────────────────────────────────────────────┐
 * │ Walking vs Running:                                                  │
 * ├──────────────────────────────────────────────────────────────────────┤
 * │ Walk:  1 tile per tick (600ms)  →  1.67 tiles/second                 │
 * │ Run:   2 tiles per tick (600ms)  →  3.33 tiles/second                │
 * │                                                                      │
 * │ Run Energy:                                                          │
 * │   - Starts at 10000 (100.00% displayed as "100")                     │
 * │   - Decreases by 1 per tile when running                             │
 * │   - Regenerates when walking/standing (not implemented here)         │
 * │   - At 0 energy, automatically switches to walking                   │
 * └──────────────────────────────────────────────────────────────────────┘
 * 
 * WAYPOINT QUEUE VISUALIZATION:
 * 
 *   Current Position: (3200, 3200)
 *   Target Position:  (3205, 3205)  ← 5 tiles east, 5 tiles north
 * 
 *   Path calculation (client-side):
 *   ┌──────────┬──────────┬───────────────────────────────────────┐
 *   │  Step #  │  (x, z)  │  Direction (from previous position)   │
 *   ├──────────┼──────────┼───────────────────────────────────────┤
 *   │    0     │(3200,3200)│         (start position)             │
 *   │    1     │(3201,3201)│  NORTHEAST (dx=+1, dz=+1)            │
 *   │    2     │(3202,3202)│  NORTHEAST (dx=+1, dz=+1)            │
 *   │    3     │(3203,3203)│  NORTHEAST (dx=+1, dz=+1)            │
 *   │    4     │(3204,3204)│  NORTHEAST (dx=+1, dz=+1)            │
 *   │    5     │(3205,3205)│  NORTHEAST (dx=+1, dz=+1)  ← goal    │
 *   └──────────┴──────────┴───────────────────────────────────────┘
 * 
 *   Queue state:
 *   ┌───────────────────────────────────────────────────────────────┐
 *   │ waypoints[0] → (3201,3201) dir=NORTHEAST                      │
 *   │ waypoints[1] → (3202,3202) dir=NORTHEAST                      │
 *   │ waypoints[2] → (3203,3203) dir=NORTHEAST                      │
 *   │ waypoints[3] → (3204,3204) dir=NORTHEAST                      │
 *   │ waypoints[4] → (3205,3205) dir=NORTHEAST                      │
 *   │ waypoint_count = 5                                            │
 *   └───────────────────────────────────────────────────────────────┘
 * 
 * TICK PROCESSING (Walking):
 *   Tick 1: Pop waypoints[0] → Move to (3201,3201), count=4
 *   Tick 2: Pop waypoints[0] → Move to (3202,3202), count=3
 *   Tick 3: Pop waypoints[0] → Move to (3203,3203), count=2
 *   Tick 4: Pop waypoints[0] → Move to (3204,3204), count=1
 *   Tick 5: Pop waypoints[0] → Move to (3205,3205), count=0 (arrived!)
 * 
 * TICK PROCESSING (Running):
 *   Tick 1: Pop waypoints[0,1] → Move to (3202,3202), count=3
 *   Tick 2: Pop waypoints[0,1] → Move to (3204,3204), count=1
 *   Tick 3: Pop waypoints[0]   → Move to (3205,3205), count=0 (arrived!)
 * 
 * ARRAY-BASED QUEUE IMPLEMENTATION:
 * 
 * This uses a simple array with shift-on-dequeue (vs circular buffer):
 * 
 *   Enqueue (add_step):
 *     waypoints[waypoint_count++] = new_point;  // O(1)
 * 
 *   Dequeue (get_next):
 *     result = waypoints[0];                     // O(1) read
 *     Shift all elements left:                   // O(n) shift
 *       waypoints[i-1] = waypoints[i];
 *     waypoint_count--;
 * 
 * COMPLEXITY ANALYSIS:
 *   - Enqueue: O(1) time
 *   - Dequeue: O(n) time where n = waypoint_count
 *   - Space:   O(1) - fixed array size
 * 
 * ALTERNATIVE IMPLEMENTATION (Circular Buffer):
 *   Could use head/tail pointers for O(1) dequeue:
 *     int head = 0, tail = 0;
 *     Enqueue: waypoints[tail++ % MAX_WAYPOINTS] = point;
 *     Dequeue: return waypoints[head++ % MAX_WAYPOINTS];
 *   
 *   Trade-off: O(1) dequeue but wastes array slots until wrap-around
 * 
 * WHY SIMPLE SHIFT APPROACH?
 *   - MAX_WAYPOINTS is small (typically 25-50)
 *   - O(n) shift is ~100 CPU cycles (negligible)
 *   - Simpler code (no modulo arithmetic)
 *   - No wasted array slots
 * 
 * WORLD COORDINATE BOUNDS:
 * 
 * RuneScape world is approximately 12800×12800 tiles:
 *   - Valid range: 0 ≤ x ≤ 12800, 0 ≤ z ≤ 12800
 *   - Actual playable area is smaller (~3100-3500 for main areas)
 *   - Bounds check prevents invalid coordinates in waypoint queue
 * 
 * OUT-OF-BOUNDS EXAMPLE:
 *   movement_add_step(handler, 99999, 99999);  // REJECTED
 *   → Prints warning: "movement_add_step out of bounds: x=99999, z=99999"
 *   → Waypoint not added to queue
 * 
 ******************************************************************************/

#ifndef MOVEMENT_H
#define MOVEMENT_H

#include "types.h"
#include "position.h"

/*******************************************************************************
 * MOVEMENTHANDLER - Player Movement State
 *******************************************************************************
 * 
 * FIELDS:
 *   waypoints:       Array of packed coordinates (Int32Array in TypeScript)
 *                    Each i32 stores: (z & 0x3fff) | ((x & 0x3fff) << 14) | ((level & 0x3) << 28)
 *                    Direction calculated dynamically on dequeue
 *                    Processed in FIFO order (queue semantics)
 * 
 *   waypoint_index:  Current last waypoint index
 *                    Range: [-1, MAX_WAYPOINTS-1]
 *                    -1 = empty queue (matches TypeScript)
 *                    0+ = index of last waypoint
 * 
 *   run_path:        Client's run toggle state
 *                    true  = player wants to run this path
 *                    false = player wants to walk this path
 * 
 *   running:         Actual running state (considers energy)
 *                    true  = currently running (run_path=true AND run_energy>0)
 *                    false = currently walking
 * 
 *   run_energy:      Energy resource for running
 *                    Range: [0, 10000] (displayed as 0-100%)
 *                    Decreases by 1 per tile when running
 *                    When reaches 0, running → false (forced walk)
 * 
 * MEMORY LAYOUT:
 * ┌──────────────────────────────────────────────────────────────────────┐
 * │ MovementHandler (on stack or in Player struct)                       │
 * ├──────────────────────────────────────────────────────────────────────┤
 * │ waypoints[0]  = 0x0032C832  (packed: level=0, x=3222, z=3218)        │
 * │ waypoints[1]  = 0x0032C833  (packed: level=0, x=3222, z=3219)        │
 * │ waypoints[2]  = 0x0032CC33  (packed: level=0, x=3223, z=3219)        │
 * │ ...                                                                  │
 * │ waypoints[MAX_WAYPOINTS-1] = (unused)                                │
 * ├──────────────────────────────────────────────────────────────────────┤
 * │ waypoint_index = 2  (0-based, so 3 waypoints total)                  │
 * │ run_path       = true                                                │
 * │ running        = true                                                │
 * │ run_energy     = 9500                                                │
 * └──────────────────────────────────────────────────────────────────────┘
 * 
 * PACKED COORDINATE FORMAT (matches TypeScript CoordGrid.packCoord):
 * 
 *   32-bit integer layout:
 *   ┌──────┬──────────────┬──────────────┐
 *   │ Bits │    Field     │   Max Value  │
 *   ├──────┼──────────────┼──────────────┤
 *   │ 0-13 │  Z coord     │   16383      │  14 bits
 *   │14-27 │  X coord     │   16383      │  14 bits
 *   │28-29 │  Level/plane │      3       │   2 bits
 *   │30-31 │  (unused)    │      -       │   2 bits
 *   └──────┴──────────────┴──────────────┘
 * 
 *   Packing formula:   (z & 0x3fff) | ((x & 0x3fff) << 14) | ((level & 0x3) << 28)
 *   Unpacking X:       (packed >> 14) & 0x3fff
 *   Unpacking Z:       packed & 0x3fff
 *   Unpacking level:   (packed >> 28) & 0x3
 * 
 * STACK ALLOCATION (no heap):
 *   - All waypoints stored inline in MovementHandler struct
 *   - No malloc/free needed (unlike old Point* approach)
 *   - Better cache locality (data contiguous in memory)
 *   - Simpler memory management (no leaks possible)
 * 
 * INVARIANTS:
 *   - -1 ≤ waypoint_index < MAX_WAYPOINTS
 *   - waypoint_index == -1 means queue is empty
 *   - waypoints[0..waypoint_index] contain valid packed coordinates
 *   - running == (run_path && run_energy > 0)
 *   - 0 ≤ run_energy ≤ 10000
 * 
 * LIFECYCLE:
 *   1. movement_init()                    → Set waypoint_index=-1, run_energy=10000
 *   2. movement_add_step() x N            → Pack and store coordinates
 *   3. movement_get_next_direction()      → Unpack, calculate direction, dequeue
 *   4. movement_destroy()                 → Set waypoint_index=-1 (no free needed)
 * 
 ******************************************************************************/
typedef struct {
    i32 waypoints[MAX_WAYPOINTS];      /* Queue of packed coordinates (FIFO) */
    i32 waypoint_index;                /* Current waypoint index (-1 = empty) */
    bool run_path;                     /* Client run toggle state */
    bool running;                      /* Actual running state (considers energy) */
    u32 run_energy;                    /* Energy resource [0, 10000] */
} MovementHandler;

/*******************************************************************************
 * PUBLIC API
 ******************************************************************************/

/*
 * movement_init - Initialize movement handler
 * 
 * @param handler  Pointer to MovementHandler to initialize
 * 
 * ALGORITHM:
 *   1. Zero-initialize entire struct (memset)
 *   2. Set waypoint_index to -1 (empty queue)
 *   3. Set run_energy to 10000 (100% energy)
 * 
 * INITIAL STATE:
 *   - waypoint_index = -1 (empty queue, matches TypeScript)
 *   - run_path       = false
 *   - running        = false
 *   - run_energy     = 10000
 *   - All waypoint values are zeroed (via memset)
 * 
 * USAGE:
 *   MovementHandler handler;
 *   movement_init(&handler);
 *   // handler is now ready for movement_add_step() calls
 * 
 * COMPLEXITY: O(1) time, O(1) space
 */
void movement_init(MovementHandler* handler);

/*
 * movement_destroy - Clear movement queue
 * 
 * @param handler  Pointer to MovementHandler to clean up
 * 
 * ALGORITHM:
 *   1. Set waypoint_index = -1 (mark queue as empty)
 * 
 * MEMORY SAFETY:
 *   - No heap allocation used, so no memory to free
 *   - Waypoints stored inline in struct (stack allocation)
 *   - Safe to call multiple times (idempotent)
 * 
 * WHEN TO CALL:
 *   - Player logout/disconnect (optional, handler will be destroyed anyway)
 *   - Server shutdown (optional)
 *   - Before movement_reset() (which calls this internally)
 * 
 * EXAMPLE:
 *   MovementHandler handler;
 *   movement_init(&handler);
 *   movement_add_step(&handler, 3200, 3200);
 *   movement_add_step(&handler, 3201, 3201);
 *   // handler.waypoint_index = 1 (two waypoints)
 *   movement_destroy(&handler);
 *   // handler.waypoint_index = -1 (empty queue)
 * 
 * COMPLEXITY: O(1) time
 */
void movement_destroy(MovementHandler* handler);

/*
 * movement_reset - Clear queue and reset movement state
 * 
 * @param handler  Pointer to MovementHandler to reset
 * 
 * ALGORITHM:
 *   1. Call movement_destroy() to clear queue
 *   2. Set run_path = false
 *   3. Set running  = false
 * 
 * WHEN TO USE:
 *   - Player stops moving (cancel current path)
 *   - Teleport (invalidate queued path)
 *   - Combat interruption (force stop)
 * 
 * NOTE: Does NOT reset run_energy (energy persists across resets)
 * 
 * EXAMPLE - TELEPORT SCENARIO:
 *   Player is walking to Varrock (25 waypoints queued)
 *   Player uses teleport spell
 *   → movement_reset(&player->movement);  // Cancel path
 *   → player->position = teleport_destination;
 * 
 * COMPLEXITY: O(1) time (no heap to free)
 */
void movement_reset(MovementHandler* handler);

/*
 * movement_add_step - Add waypoint to movement queue
 * 
 * @param handler  Pointer to MovementHandler
 * @param x        World X coordinate of waypoint
 * @param z        World Z coordinate of waypoint
 * 
 * ALGORITHM:
 *   1. Check queue capacity: if full (index >= MAX_WAYPOINTS-1), reject
 *   2. Validate coordinates: if out of bounds (x>12800 or z>12800), reject
 *   3. Pack coordinates: coord_pack(0, x, z)
 *   4. Store in queue: waypoints[++waypoint_index] = packed
 * 
 * NOTE: Direction is NOT stored (unlike old implementation)
 *   Direction calculated dynamically on dequeue (matches TypeScript)
 *   This allows same waypoint to be approached from different positions
 * 
 * COORDINATE PACKING (matches TypeScript CoordGrid.packCoord):
 *   ┌────────────────────────────────────────────────────────┐
 *   │ Example: x=3222, z=3218, level=0                       │
 *   ├────────────────────────────────────────────────────────┤
 *   │ Binary breakdown:                                      │
 *   │   z     = 3218 = 0b110010010010                        │
 *   │   x     = 3222 = 0b110010010110                        │
 *   │   level = 0    = 0b00                                  │
 *   │                                                        │
 *   │ Packed result (32-bit):                                │
 *   │   Bits  0-13: z     = 0b00110010010010                 │
 *   │   Bits 14-27: x     = 0b00110010010110                 │
 *   │   Bits 28-29: level = 0b00                             │
 *   │   Bits 30-31: unused = 0b00                            │
 *   │                                                        │
 *   │ Result: 0x0032C832 (packed coordinate)                 │
 *   └────────────────────────────────────────────────────────┘
 * 
 * REJECTION SCENARIOS:
 *   1. Queue full:
 *        movement_add_step(handler, 3200, 3200);  // 25 times (MAX_WAYPOINTS)
 *        movement_add_step(handler, 3200, 3200);  // REJECTED (silently)
 * 
 *   2. Out of bounds:
 *        movement_add_step(handler, 50000, 50000);
 *        → Prints: "WARNING: movement_add_step out of bounds: x=50000, z=50000"
 * 
 * MEMORY EFFICIENCY:
 *   No heap allocation (stored inline in array)
 *   Each waypoint: 4 bytes (packed i32)
 *   Max memory: MAX_WAYPOINTS × 4 = 100 bytes (25 waypoints)
 *   
 *   Old Point* approach: MAX_WAYPOINTS × 16 bytes = 400 bytes
 *   Improvement: 4x less memory, better cache locality
 * 
 * COMPLEXITY: O(1) time, O(1) space (no allocation)
 */
void movement_add_step(MovementHandler* handler, u32 x, u32 z);

/*
 * coord_pack - Pack coordinates into 32-bit integer
 * 
 * @param level  Plane level (0-3)
 * @param x      X coordinate (0-16383)
 * @param z      Z coordinate (0-16383)
 * @return       Packed coordinate (matches TypeScript CoordGrid.packCoord)
 * 
 * PACKING FORMULA:
 *   (z & 0x3fff) | ((x & 0x3fff) << 14) | ((level & 0x3) << 28)
 * 
 * BIT LAYOUT:
 *   [31-30]: unused (2 bits)
 *   [29-28]: level  (2 bits) - max value 3
 *   [27-14]: x      (14 bits) - max value 16383
 *   [13-0]:  z      (14 bits) - max value 16383
 * 
 * EXAMPLE:
 *   coord_pack(0, 3222, 3218) = 0x0032C832
 * 
 * COMPLEXITY: O(1) time
 */
u32 coord_pack(u32 level, u32 x, u32 z);

/*
 * coord_unpack - Unpack 32-bit integer into coordinates
 * 
 * @param packed  Packed coordinate value
 * @param level   Output: plane level (0-3)
 * @param x       Output: X coordinate (0-16383)
 * @param z       Output: Z coordinate (0-16383)
 * 
 * UNPACKING FORMULA:
 *   z     = packed & 0x3fff
 *   x     = (packed >> 14) & 0x3fff
 *   level = (packed >> 28) & 0x3
 * 
 * EXAMPLE:
 *   u32 level, x, z;
 *   coord_unpack(0x0032C832, &level, &x, &z);
 *   // level=0, x=3222, z=3218
 * 
 * COMPLEXITY: O(1) time
 */
void coord_unpack(u32 packed, u32* level, u32* x, u32* z);

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
 *   1. Walk diagonally while both dx and dz are non-zero
 *   2. Walk horizontally or vertically for remaining distance
 * 
 * EXAMPLE:
 *   From (3200, 3200) to (3205, 3203):
 *     dx = +5, dz = +3
 *     Diagonal steps: (3201,3201), (3202,3202), (3203,3203)  [3 steps]
 *     Horizontal steps: (3204,3203), (3205,3203)             [2 steps]
 *     Total: 5 waypoints
 * 
 * NOTE: This does NOT check collision - walks straight through walls!
 *       For production use, implement proper A* pathfinding with collision.
 * 
 * COMPLEXITY: O(N) where N = Manhattan distance = |dx| + |dz|
 */
void movement_naive_path(MovementHandler* handler, u32 src_x, u32 src_z, u32 dest_x, u32 dest_z);

/*
 * movement_set_run_path - Set run toggle state
 * 
 * @param handler  Pointer to MovementHandler
 * @param running  true = attempt to run, false = walk
 * 
 * ALGORITHM:
 *   1. Set run_path = running
 *   2. Update actual running state:
 *        handler->running = (running && handler->run_energy > 0)
 * 
 * RUNNING STATE LOGIC:
 *   ┌───────────┬──────────────┬──────────────────────────────┐
 *   │ run_path  │ run_energy   │  Actual running state        │
 *   ├───────────┼──────────────┼──────────────────────────────┤
 *   │  false    │   (any)      │  false (walking)             │
 *   │  true     │   > 0        │  true  (running)             │
 *   │  true     │   = 0        │  false (walk, no energy)     │
 *   └───────────┴──────────────┴──────────────────────────────┘
 * 
 * USAGE - CLIENT RUN TOGGLE:
 *   Player clicks run orb:
 *     movement_set_run_path(&player->movement, true);
 *   
 *   Player clicks walk orb:
 *     movement_set_run_path(&player->movement, false);
 * 
 * USAGE - PATH COMMAND:
 *   Client packet format:
 *     Opcode: WALK_OPCODE or RUN_OPCODE
 *     Payload: [x0,z0, x1,z1, x2,z2, ...]
 *   
 *   Server processing:
 *     bool is_run_packet = (opcode == RUN_OPCODE);
 *     movement_set_run_path(&player->movement, is_run_packet);
 *     for each (x, z) in packet:
 *       movement_add_step(&player->movement, x, z);
 * 
 * COMPLEXITY: O(1) time
 */
void movement_set_run_path(MovementHandler* handler, bool running);

/*
 * movement_finish - Finalize movement path
 * 
 * @param handler  Pointer to MovementHandler
 * 
 * CURRENT IMPLEMENTATION:
 *   Empty function (placeholder for future features)
 * 
 * POTENTIAL FUTURE USE:
 *   - Mark path as "complete" for pathfinding
 *   - Trigger arrival animation/event
 *   - Calculate total path distance for statistics
 * 
 * COMPLEXITY: O(1) time
 */
void movement_finish(MovementHandler* handler);

/*
 * movement_get_next_direction - Calculate and return next movement direction
 * 
 * @param handler    Pointer to MovementHandler
 * @param current_x  Player's current X position
 * @param current_z  Player's current Z position
 * @return           Direction constant (0-7), or -1 if no movement
 * 
 * ALGORITHM (matches TypeScript PathingEntity.validateAndAdvanceStep):
 *   1. If queue empty (waypoint_index == -1), return -1
 *   2. Unpack first waypoint: coord_unpack(waypoints[0], &level, &x, &z)
 *   3. Calculate direction: position_direction(x - current_x, z - current_z)
 *   4. If direction == -1 (waypoint reached):
 *        a. Dequeue waypoint (shift array left)
 *        b. Decrement waypoint_index
 *        c. Recursively call self (like TypeScript recursive validation)
 *   5. Else (valid direction):
 *        a. Dequeue waypoint (shift array left)
 *        b. Decrement waypoint_index
 *        c. Decrease run energy if running
 *        d. Return direction
 * 
 * RECURSIVE VALIDATION (matches TypeScript):
 *   When player is already at waypoint destination (direction == -1):
 *   - Dequeues waypoint without moving
 *   - Recursively checks next waypoint
 *   - Continues until finding valid move or queue empty
 * 
 * DEQUEUE VISUALIZATION:
 *   Before:
 *   ┌──────────────────────────────────────────────────────────┐
 *   │ waypoints[0] = 0x0032C833  (x=3222, z=3219)              │
 *   │ waypoints[1] = 0x0032CC33  (x=3223, z=3219)              │
 *   │ waypoints[2] = 0x0032CC34  (x=3223, z=3220)              │
 *   │ waypoint_index = 2                                       │
 *   │ Current pos: (3222, 3218)                                │
 *   └──────────────────────────────────────────────────────────┘
 * 
 *   Calculate: dx = 3222-3222 = 0, dz = 3219-3218 = 1
 *   Direction: NORTH (1)
 * 
 *   After movement_get_next_direction():
 *   ┌──────────────────────────────────────────────────────────┐
 *   │ waypoints[0] = 0x0032CC33  (x=3223, z=3219)  ← shifted   │
 *   │ waypoints[1] = 0x0032CC34  (x=3223, z=3220)  ← shifted   │
 *   │ waypoint_index = 1                                       │
 *   └──────────────────────────────────────────────────────────┘
 * 
 *   Returned: 1 (NORTH)
 * 
 * CALLER USAGE (no memory management needed):
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
 * ENERGY DEPLETION:
 *   When running, each step costs 1 energy:
 *     run_energy:   10000 → 9999 → 9998 → ... → 1 → 0
 *     At 0 energy: running becomes false (automatic walk)
 * 
 * GAME TICK INTEGRATION:
 *   Every 600ms game tick:
 *     if (movement_is_moving(&player->movement)) {
 *       i32 dir = movement_get_next_direction(&player->movement,
 *                                             player->position.x,
 *                                             player->position.z);
 *       if (dir != -1) {
 *         position_move(&player->position, 
 *                      DIRECTION_DELTA_X[dir], 
 *                      DIRECTION_DELTA_Z[dir]);
 *         send_position_update(player);
 *       }
 *     }
 * 
 * COMPLEXITY: O(n) time worst case (recursive dequeue), usually O(n) for shift
 */
i32 movement_get_next_direction(MovementHandler* handler, u32 current_x, u32 current_z);

/*
 * movement_is_moving - Check if player has queued waypoints
 * 
 * @param handler  Pointer to MovementHandler
 * @return         true if waypoint_count > 0, false otherwise
 * 
 * USAGE:
 *   if (movement_is_moving(&player->movement)) {
 *     // Process movement this tick
 *     Point* next = movement_get_next(&player->movement);
 *     // ...
 *   } else {
 *     // Player is standing still, no movement update needed
 *   }
 * 
 * COMPLEXITY: O(1) time
 */
bool movement_is_moving(const MovementHandler* handler);

/*
 * movement_get_waypoint_count - Get number of queued waypoints
 * 
 * @param handler  Pointer to MovementHandler
 * @return         Number of waypoints in queue [0, MAX_WAYPOINTS]
 * 
 * USAGE EXAMPLES:
 *   - Estimate time to arrival: ticks = waypoint_count / (running ? 2 : 1)
 *   - Check queue capacity: if (waypoint_count < MAX_WAYPOINTS) { add_step(); }
 *   - Display progress: printf("Progress: %u/%u steps\n", steps_taken, waypoint_count)
 * 
 * COMPLEXITY: O(1) time
 */
u32 movement_get_waypoint_count(const MovementHandler* handler);

/*
 * movement_remove_first_waypoint - Remove first waypoint without returning it
 * 
 * @param handler  Pointer to MovementHandler
 * 
 * ALGORITHM:
 *   1. If queue empty (waypoint_count == 0), return (no-op)
 *   2. Free first waypoint: free(waypoints[0])
 *   3. Shift remaining waypoints left (same as movement_get_next)
 *   4. Decrement count: waypoint_count--
 * 
 * DIFFERENCE FROM movement_get_next:
 *   - Does NOT allocate copy (no malloc)
 *   - Does NOT return Point*
 *   - Does NOT decrease run_energy
 * 
 * WHEN TO USE:
 *   - Discard invalid waypoint without processing
 *   - Skip waypoint due to collision/obstacle
 *   - Fast-forward through waypoints (e.g., teleport to end of path)
 * 
 * USAGE - COLLISION HANDLING:
 *   Point* next = movement_get_next(&player->movement);
 *   if (is_blocked(next->x, next->z)) {
 *     // Can't move there, put it back and wait
 *     movement_add_step(&player->movement, next->x, next->z);  // Re-queue
 *     free(next);
 *   } else {
 *     player->position = *next;
 *     free(next);
 *   }
 * 
 *   Alternative approach (discard blocked waypoint):
 *   Point* next = movement_get_next(&player->movement);
 *   if (is_blocked(next->x, next->z)) {
 *     free(next);  // Discard this waypoint
 *     // Player stops (or continue to next waypoint)
 *   }
 * 
 * COMPLEXITY: O(n) time where n = waypoint_count
 */
void movement_remove_first_waypoint(MovementHandler* handler);

#endif /* MOVEMENT_H */
