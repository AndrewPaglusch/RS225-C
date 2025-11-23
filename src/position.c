/*******************************************************************************
 * POSITION.C - Spatial Coordinate System Implementation
 *******************************************************************************
 * 
 * This file implements spatial positioning and direction calculations for:
 *   - 3D coordinate initialization and movement
 *   - Hierarchical coordinate transformations (absolute ↔ region ↔ local)
 *   - Direction encoding from movement deltas
 *   - Viewport culling for network optimization
 * 
 * KEY ALGORITHMS IMPLEMENTED:
 *   1. Bit-shift coordinate conversion (fast division/multiplication by 8)
 *   2. Direction lookup from delta signs (3-bit compass encoding)
 *   3. Rectangular viewport culling (Manhattan distance check)
 *   4. Region-based spatial partitioning
 * 
 * EDUCATIONAL CONCEPTS:
 *   - Spatial partitioning for large game worlds
 *   - Bit manipulation for performance optimization
 *   - Relative coordinate systems for bandwidth reduction
 *   - Viewport culling for scalability
 *   - Direction encoding for compact protocol
 * 
 * COORDINATE SYSTEM SUMMARY:
 * 
 *   WORLD SPACE (absolute coordinates):
 *   - 16384 x 16384 tiles (14-bit addressing)
 *   - Origin at (0, 0)
 *   - X increases eastward, Z increases northward
 * 
 *   REGION SPACE (for spatial indexing):
 *   - World divided into 64x64 tile regions
 *   - Each region is 8x8 chunks (8 tiles per chunk)
 *   - Region ID calculated by dividing absolute coords by 8
 * 
 *   LOCAL SPACE (for network protocol):
 *   - Coordinates relative to region base
 *   - Range: [0, 63] for entities in same region
 *   - Allows 6-bit encoding instead of 14-bit
 * 
 * BIT MANIPULATION TECHNIQUES:
 * 
 *   Division by 8 using right shift:
 *     x >> 3  ≡  x / 8
 *     Example: 3232 >> 3 = 404
 * 
 *   Multiplication by 8 using left shift:
 *     x << 3  ≡  x * 8
 *     Example: 404 << 3 = 3232
 * 
 *   Why shifts are faster than division:
 *     - Right shift: ~1 CPU cycle
 *     - Division: ~20-40 CPU cycles (depending on architecture)
 * 
 * SPATIAL PARTITIONING DIAGRAM:
 * 
 *   ┌─────────────────────────────────────────────────┐
 *   │              WORLD (16384 x 16384)              │
 *   │  ┌────────┬────────┬────────┬────────┐          │
 *   │  │Region  │Region  │Region  │Region  │          │
 *   │  │(0,0)   │(1,0)   │(2,0)   │(3,0)   │          │
 *   │  │64x64   │64x64   │64x64   │64x64   │          │
 *   │  ├────────┼────────┼────────┼────────┤          │
 *   │  │Region  │Region  │Region  │Region  │  ...     │
 *   │  │(0,1)   │(1,1)   │(2,1)   │(3,1)   │          │
 *   │  │64x64   │64x64   │64x64   │64x64   │          │
 *   │  ├────────┼────────┼────────┼────────┤          │
 *   │  │  ...   │  ...   │  ...   │  ...   │          │
 *   │  └────────┴────────┴────────┴────────┘          │
 *   └─────────────────────────────────────────────────┘
 * 
 ******************************************************************************/

#include "position.h"
#include <stdlib.h>

/*******************************************************************************
 * POSITION LIFECYCLE
 ******************************************************************************/

/*
 * position_init - Initialize position with absolute coordinates
 * 
 * @param pos     Position structure to initialize
 * @param x       East/West coordinate (0-16383)
 * @param z       North/South coordinate (0-16383)
 * @param height  Plane level (0-3)
 * 
 * MEMORY LAYOUT AFTER INITIALIZATION:
 *   ┌────────────┬────────────┬────────────┐
 *   │   x=3232   │   z=3232   │  height=0  │  12 bytes total
 *   └────────────┴────────────┴────────────┘
 * 
 * COORDINATE VALIDATION:
 *   This function does NOT validate coordinates!
 *   Caller should ensure:
 *     - x < 16384 (14-bit limit)
 *     - z < 16384 (14-bit limit)
 *     - height <= 3 (2-bit limit)
 * 
 * EXAMPLE USAGE:
 *   Position lumbridge_castle;
 *   position_init(&lumbridge_castle, 3222, 3218, 0);
 * 
 *   Position varrock_square;
 *   position_init(&varrock_square, 3212, 3424, 0);
 * 
 * COMPLEXITY: O(1) time, O(1) space (stack allocation)
 */
void position_init(Position* pos, u32 x, u32 z, u32 height) {
    /* Direct field assignment (no validation) */
    pos->x = x;
    pos->z = z;
    pos->height = height;
}

/*******************************************************************************
 * POSITION MOVEMENT
 ******************************************************************************/

/*
 * position_move - Apply relative movement to position
 * 
 * @param pos  Position to modify (in-place)
 * @param dx   X displacement (negative=west, positive=east)
 * @param dz   Z displacement (negative=south, positive=north)
 * 
 * ALGORITHM:
 *   Simple vector addition:
 *     new_x = old_x + dx
 *     new_z = old_z + dz
 * 
 * MOVEMENT VECTOR DIAGRAM:
 * 
 *   Before:                After:
 *   ┌─────────────┐        ┌─────────────┐
 *   │             │        │       * NEW │  NEW = (x+dx, z+dz)
 *   │             │        │      ↗      │
 *   │      * OLD  │        │  ↗          │  Vector: (dx, dz)
 *   │   (x, z)    │        │ * OLD       │
 *   └─────────────┘        └─────────────┘
 * 
 * EXAMPLE:
 *   Position pos;
 *   position_init(&pos, 3200, 3200, 0);
 * 
 *   // Move 5 tiles east, 3 tiles south
 *   position_move(&pos, 5, -3);
 * 
 *   // Result: pos.x = 3205, pos.z = 3197
 * 
 * DIRECTION MOVEMENT EXAMPLE:
 *   // Move one tile in direction DIR_NE (northeast)
 *   int dir = DIR_NE;
 *   int dx = DIRECTION_DELTA_X[dir];  // +1
 *   int dz = DIRECTION_DELTA_Z[dir];  // +1
 *   position_move(&pos, dx, dz);
 * 
 * SIGNED ARITHMETIC:
 *   dx and dz are signed (i32) to allow negative movement
 *   pos->x and pos->z are unsigned (u32)
 *   C automatically handles signed→unsigned conversion
 * 
 * OVERFLOW BEHAVIOR:
 *   If x + dx < 0, result wraps to large positive (undefined behavior)
 *   If x + dx > 16383, result may exceed valid map bounds
 *   Caller should validate before calling or after returning
 * 
 * COLLISION DETECTION:
 *   This function does NOT check:
 *     - Map boundaries
 *     - Walkable tiles
 *     - Object collisions
 *   These checks must be done separately in game logic
 * 
 * COMPLEXITY: O(1) time
 */
void position_move(Position* pos, i32 dx, i32 dz) {
    /* Apply displacement using signed addition */
    pos->x += dx;  /* Implicit signed→unsigned conversion */
    pos->z += dz;
}

/*******************************************************************************
 * COORDINATE TRANSFORMATIONS - Region Calculation
 ******************************************************************************/

/*
 * position_get_region_x - Convert absolute X to region X coordinate
 * 
 * @param pos  Position with absolute coordinates
 * @return     Region X coordinate
 * 
 * ALGORITHM:
 *   region_x = (absolute_x >> 3) - 6
 * 
 * BREAKDOWN:
 *   Step 1: Divide by 8 using right shift
 *     absolute_x >> 3  ≡  floor(absolute_x / 8)
 * 
 *   Step 2: Subtract offset
 *     The -6 accounts for RuneScape's map origin offset
 *     (historical artifact from original game design)
 * 
 * BIT-LEVEL EXAMPLE:
 *   Input: absolute_x = 3232
 * 
 *   Binary representation:
 *     3232 = 0b110010100000  (12 bits)
 * 
 *   Right shift by 3:
 *     3232 >> 3 = 0b110010100 = 404
 * 
 *   Subtract offset:
 *     404 - 6 = 398
 * 
 *   Result: region_x = 398
 * 
 * REGION BOUNDARY CALCULATION:
 *   Each region covers 64 tiles (8 chunks × 8 tiles/chunk)
 * 
 *   Region 398 boundaries:
 *     Min X: 398 << 3 = 3184
 *     Max X: 3184 + 63 = 3247
 * 
 *   Therefore position 3232 is in region 398 ✓
 * 
 * WHY BIT SHIFT INSTEAD OF DIVISION?
 * 
 *   Performance comparison (typical x86-64):
 *     Division:   20-40 CPU cycles
 *     Bit shift:  1 CPU cycle
 * 
 *   Assembly comparison:
 *     Division:  MOV EAX, x
 *                MOV ECX, 8
 *                DIV ECX         ; slow operation
 * 
 *     Bit shift: MOV EAX, x
 *                SHR EAX, 3      ; single instruction
 * 
 * REGION SIZE DIAGRAM:
 * 
 *   ┌────────────────────────────────────────────┐
 *   │  Region 398 (64 tiles × 64 tiles)          │
 *   │  ┌──────────────────────────────────────┐  │
 *   │  │  Absolute coordinates:               │  │
 *   │  │    Min: (3184, 3184)                 │  │
 *   │  │    Max: (3247, 3247)                 │  │
 *   │  │                                      │  │
 *   │  │         * (3232, 3232)               │  │
 *   │  │      (example position)              │  │
 *   │  └──────────────────────────────────────┘  │
 *   └────────────────────────────────────────────┘
 * 
 * COMPLEXITY: O(1) time (single shift and subtract)
 */
u32 position_get_zone_x(const Position* pos) {
    return pos->x >> 3;
}

u32 position_get_zone_center_x(const Position* pos) {
    return (pos->x >> 3) - 6;
}

u32 position_get_mapsquare_x(const Position* pos) {
    return pos->x >> 6;
}

/*
 * position_get_region_z - Convert absolute Z to region Z coordinate
 * 
 * @param pos  Position with absolute coordinates
 * @return     Region Z coordinate
 * 
 * ALGORITHM: Same as position_get_region_x but for Z axis
 *   region_z = (absolute_z >> 3) - 6
 * 
 * EXAMPLE:
 *   Input: absolute_z = 3200
 * 
 *   Step 1: 3200 >> 3
 *     Binary: 0b110010000000 >> 3 = 0b110010000 = 400
 * 
 *   Step 2: 400 - 6 = 394
 * 
 *   Result: region_z = 394
 * 
 * REGION COVERAGE:
 *   Region 394 Z boundaries:
 *     Min Z: 394 << 3 = 3152
 *     Max Z: 3152 + 63 = 3215
 * 
 *   Position 3200 is in region 394 ✓
 * 
 * COMPLEXITY: O(1) time
 */
u32 position_get_zone_z(const Position* pos) {
    return pos->z >> 3;
}

u32 position_get_zone_center_z(const Position* pos) {
    return (pos->z >> 3) - 6;
}

u32 position_get_mapsquare_z(const Position* pos) {
    return pos->z >> 6;
}

/*******************************************************************************
 * COORDINATE TRANSFORMATIONS - Local Coordinates
 ******************************************************************************/

/*
 * position_get_local_x - Get local X coordinate relative to base position
 * 
 * @param pos   Position to convert (e.g., NPC location)
 * @param base  Base position (e.g., player location)
 * @return      Local X offset relative to base's region
 * 
 * ALGORITHM:
 *   1. Calculate base's region:    region = (base->x >> 3) - 6
 *   2. Convert region to absolute: region_base = region << 3
 *   3. Calculate offset:           local = pos->x - region_base
 * 
 * STEP-BY-STEP EXAMPLE:
 *   Player position (base):  (3232, 3232)
 *   NPC position (pos):      (3240, 3235)
 * 
 *   Step 1: Calculate base's region X
 *     region_x = (3232 >> 3) - 6
 *     region_x = 404 - 6
 *     region_x = 398
 * 
 *   Step 2: Convert region back to absolute base
 *     region_base_x = 398 << 3
 *     region_base_x = 3184
 * 
 *   Step 3: Calculate local offset
 *     local_x = 3240 - 3184
 *     local_x = 56
 * 
 *   Result: NPC is at local X = 56 (within 0-63 range)
 * 
 * VISUAL DIAGRAM:
 * 
 *   ┌─────────────────────────────────────────────────┐
 *   │  Region 398 (base at 3184)                      │
 *   │  ┌───────────────────────────────────────────┐  │
 *   │  │ 0                                      63 │  │ Local X
 *   │  │ ├────────────────────────────────────────┤│  │
 *   │  │                                           │  │
 *   │  │           @ Player (local X = 48)         │  │
 *   │  │           (absolute X = 3232)             │  │
 *   │  │                                           │  │
 *   │  │                     * NPC (local X = 56)  │  │
 *   │  │                     (absolute X = 3240)   │  │
 *   │  └───────────────────────────────────────────┘  │
 *   │  3184                                    3247   │ Absolute X
 *   └─────────────────────────────────────────────────┘
 * 
 * NETWORK PROTOCOL OPTIMIZATION:
 * 
 *   Without local coordinates:
 *     Send absolute X: 3240 (requires 14 bits)
 * 
 *   With local coordinates:
 *     Send local X: 56 (requires 6 bits)
 * 
 *   Bandwidth savings: 14 - 6 = 8 bits per coordinate
 *   For 100 NPCs: 100 × 2 coords × 8 bits = 1600 bits = 200 bytes saved!
 * 
 * CROSS-REGION ENTITIES:
 *   If NPC is in different region than player:
 *     local_x may be < 0 or > 63
 *   Protocol handles this by:
 *     - Sending full absolute coordinates, OR
 *     - Using extended local coordinate format
 * 
 * BIT MANIPULATION BREAKDOWN:
 * 
 *   region_x calculation:
 *     (base->x >> 3) - 6
 *     = (3232 >> 3) - 6
 *     = 404 - 6
 *     = 398
 * 
 *   Region base calculation:
 *     region_x << 3
 *     = 398 << 3
 *     = 398 × 8
 *     = 3184
 * 
 *   Local offset:
 *     pos->x - region_base
 *     = 3240 - 3184
 *     = 56
 * 
 * COMPLEXITY: O(1) time (3 arithmetic operations)
 */
u32 position_get_local_x(const Position* pos, const Position* base) {
    return pos->x - (position_get_zone_center_x(base) << 3);
}

/*
 * position_get_local_z - Get local Z coordinate relative to base position
 * 
 * @param pos   Position to convert (e.g., NPC location)
 * @param base  Base position (e.g., player location)
 * @return      Local Z offset relative to base's region
 * 
 * ALGORITHM: Same as position_get_local_x but for Z axis
 *   1. region_z = (base->z >> 3) - 6
 *   2. region_base_z = region_z << 3
 *   3. local_z = pos->z - region_base_z
 * 
 * EXAMPLE:
 *   Player position (base):  (3232, 3232)
 *   NPC position (pos):      (3240, 3235)
 * 
 *   Step 1: region_z = (3232 >> 3) - 6 = 398
 *   Step 2: region_base_z = 398 << 3 = 3184
 *   Step 3: local_z = 3235 - 3184 = 51
 * 
 *   Result: NPC is at local Z = 51
 * 
 * COORDINATE PAIR:
 *   From previous examples:
 *     local_x = 56
 *     local_z = 51
 * 
 *   NPC is at (56, 51) in local coordinate space
 *   Relative to region base at (3184, 3184)
 * 
 * COMPLEXITY: O(1) time
 */
u32 position_get_local_z(const Position* pos, const Position* base) {
    return pos->z - (position_get_zone_center_z(base) << 3);
}

/*******************************************************************************
 * DIRECTION ENCODING
 ******************************************************************************/

/*
 * position_direction - Calculate compass direction from movement deltas
 * 
 * @param dx  X displacement (-1, 0, or +1)
 * @param dz  Z displacement (-1, 0, or +1)
 * @return    Direction constant (0-7), or -1 if no movement
 * 
 * ALGORITHM: Decision tree based on delta signs
 * 
 * DECISION TREE DIAGRAM:
 * 
 *                    ┌────────┐
 *                    │  dx?   │
 *                    └───┬────┘
 *         ┌──────────────┼──────────────┐
 *      dx < 0          dx = 0         dx > 0
 *         │              │              │
 *    ┌────┴────┐    ┌────┴────┐    ┌────┴────┐
 *    │   dz?   │    │   dz?   │    │   dz?   │
 *    └────┬────┘    └────┬────┘    └────┬────┘
 *    ┌────┼────┐    ┌────┼────┐    ┌────┼────┐
 * dz<0 dz=0 dz>0 dz<0 dz=0 dz>0 dz<0 dz=0 dz>0
 *   │    │    │    │    │    │    │    │    │
 * SW(5) W(3) NW(0) S(6) -1  N(1) SE(7) E(4) NE(2)
 * 
 * TRUTH TABLE:
 * 
 *   DX  │ DZ  │ Result │ Name  │ Binary
 *   ────┼─────┼────────┼───────┼────────
 *   -1  │ +1  │   0    │  NW   │ 0b000
 *    0  │ +1  │   1    │  N    │ 0b001
 *   +1  │ +1  │   2    │  NE   │ 0b010
 *   -1  │  0  │   3    │  W    │ 0b011
 *    0  │  0  │  -1    │ (none)│   -
 *   +1  │  0  │   4    │  E    │ 0b100
 *   -1  │ -1  │   5    │  SW   │ 0b101
 *    0  │ -1  │   6    │  S    │ 0b110
 *   +1  │ -1  │   7    │  SE   │ 0b111
 * 
 * EXAMPLE 1: Northeast movement
 *   Player at (3232, 3232)
 *   Moves to (3233, 3233)
 * 
 *   dx = 3233 - 3232 = +1
 *   dz = 3233 - 3232 = +1
 * 
 *   Decision: dx > 0, so check right branch
 *             dz > 0, so return DIR_NE
 * 
 *   Result: 2 (DIR_NE)
 * 
 * EXAMPLE 2: South movement
 *   Player at (3232, 3232)
 *   Moves to (3232, 3231)
 * 
 *   dx = 3232 - 3232 = 0
 *   dz = 3231 - 3232 = -1
 * 
 *   Decision: dx = 0, so check middle branch
 *             dz < 0, so return DIR_S
 * 
 *   Result: 6 (DIR_S)
 * 
 * EXAMPLE 3: No movement
 *   Player at (3232, 3232)
 *   Stays at (3232, 3232)
 * 
 *   dx = 0
 *   dz = 0
 * 
 *   Decision: dx = 0, dz = 0
 * 
 *   Result: -1 (no movement)
 * 
 * COMPASS VISUALIZATION:
 * 
 *         N (1)
 *          ↑
 *   NW(0) ╱│╲ NE(2)
 *        ╱ │ ╲
 *  W(3)─┼──+──┼─ E(4)
 *        ╲ │ ╱
 *   SW(5) ╲│╱ SE(7)
 *          ↓
 *         S (6)
 * 
 * PROTOCOL ENCODING:
 *   Directions 0-7 fit in 3 bits
 *   RuneScape movement packets use bit packing:
 *     - 1 bit: has movement (0=no, 1=yes)
 *     - 3 bits: direction (if has movement)
 *     - Total: 4 bits per entity instead of 8
 * 
 * PERFORMANCE:
 *   Worst case: 6 comparisons (3 levels of if-else)
 *   Best case: 2 comparisons (early return)
 *   Average: ~4 comparisons
 * 
 *   Alternative: Lookup table would use:
 *     - 3×3×4 = 36 bytes of memory
 *     - Index calculation: ((dx+1)*3 + (dz+1))*4
 *     - Not worth the memory overhead for this case
 * 
 * COMPLEXITY: O(1) time (constant comparisons)
 */
i32 position_direction(i32 dx, i32 dz) {
    if (dx < 0) {
        /* Moving west (negative X) */
        if (dz < 0) return DIR_SW;  /* Southwest: dx=-1, dz=-1 → 5 */
        if (dz > 0) return DIR_NW;  /* Northwest: dx=-1, dz=+1 → 0 */
        return DIR_W;               /* West:      dx=-1, dz= 0 → 3 */
    } else if (dx > 0) {
        /* Moving east (positive X) */
        if (dz < 0) return DIR_SE;  /* Southeast: dx=+1, dz=-1 → 7 */
        if (dz > 0) return DIR_NE;  /* Northeast: dx=+1, dz=+1 → 2 */
        return DIR_E;               /* East:      dx=+1, dz= 0 → 4 */
    } else {
        /* No horizontal movement (dx = 0) */
        if (dz < 0) return DIR_S;   /* South:     dx= 0, dz=-1 → 6 */
        if (dz > 0) return DIR_N;   /* North:     dx= 0, dz=+1 → 1 */
        return -1;                  /* No movement: dx=0, dz=0 → -1 */
    }
}

/*******************************************************************************
 * VIEWPORT CULLING
 ******************************************************************************/

/*
 * position_is_viewable_from - Check if position is within viewport range
 * 
 * @param pos    Position to check (e.g., NPC location)
 * @param other  Observer position (e.g., player location)
 * @return       true if pos is visible from other, false otherwise
 * 
 * ALGORITHM: Rectangular bounds check (Manhattan distance)
 *   1. Calculate deltas: dx = other->x - pos->x
 *                        dz = other->z - pos->z
 *   2. Check bounds:     -15 ≤ dx ≤ 14  AND  -15 ≤ dz ≤ 14
 * 
 * VIEWPORT DIMENSIONS:
 * 
 *   RuneScape uses a 30×30 tile viewport:
 *   ┌────────────────────────────────────────┐
 *   │  Northwest corner: (-15, +14)          │
 *   │  ┌──────────────────────────────────┐  │
 *   │  │                                  │  │
 *   │  │  15 tiles                        │  │
 *   │  │  west                            │  │
 *   │  │   │                              │  │
 *   │  │   ├→ @ Observer (0, 0)           │  │
 *   │  │      │                           │  │
 *   │  │      └→ 14 tiles east            │  │
 *   │  │                                  │  │
 *   │  └──────────────────────────────────┘  │
 *   │  Southeast corner: (+14, -15)          │
 *   └────────────────────────────────────────┘
 * 
 *   Total visible area: 30 × 30 = 900 tiles
 * 
 * WHY ASYMMETRIC BOUNDS?
 *   Historical design decision from original RuneScape
 *   Likely for screen alignment or camera positioning
 *   Could be related to isometric view rendering
 * 
 * COORDINATE DELTA DIAGRAM:
 * 
 *   Observer at (3232, 3232) [marked as @]
 *   Target at various positions [marked as *]
 * 
 *   ┌─────────────────────────────────────────┐
 *   │                                         │  North
 *   │  (-15,+14)               (+14,+14)      │    ↑
 *   │      ┌─────────────────────────┐        │    │
 *   │      │                         │        │    │
 *   │      │  *A (dx=-10, dz=+5)     │        │    │
 *   │      │  VISIBLE ✓              │        │    │
 *   │      │                         │        │    └──→ East
 *   │      │       @ (0, 0)          │        │
 *   │      │     Observer            │        │
 *   │      │                         │        │
 *   │      │              *B (dx=+20, dz=-5)  │
 *   │      └─────────────────────────┘   OUT OF RANGE ✗
 *   │  (-15,-15)               (+14,-15)      │
 *   │                                         │
 *   └─────────────────────────────────────────┘
 * 
 * EXAMPLE 1: Entity within viewport
 *   Observer: (3232, 3232)
 *   Target:   (3240, 3235)
 * 
 *   dx = 3240 - 3232 = +8
 *   dz = 3235 - 3232 = +3
 * 
 *   Check X: -15 ≤ 8 ≤ 14? YES ✓
 *   Check Z: -15 ≤ 3 ≤ 14? YES ✓
 * 
 *   Result: true (entity is visible)
 * 
 * EXAMPLE 2: Entity too far east
 *   Observer: (3232, 3232)
 *   Target:   (3250, 3232)
 * 
 *   dx = 3250 - 3232 = +18
 *   dz = 3232 - 3232 = 0
 * 
 *   Check X: -15 ≤ 18 ≤ 14? NO (18 > 14) ✗
 * 
 *   Result: false (entity is out of viewport)
 * 
 * EXAMPLE 3: Entity too far west
 *   Observer: (3232, 3232)
 *   Target:   (3215, 3232)
 * 
 *   dx = 3215 - 3232 = -17
 *   dz = 3232 - 3232 = 0
 * 
 *   Check X: -15 ≤ -17 ≤ 14? NO (-17 < -15) ✗
 * 
 *   Result: false (entity is out of viewport)
 * 
 * EXAMPLE 4: Edge case - exactly on boundary
 *   Observer: (3232, 3232)
 *   Target:   (3246, 3232)
 * 
 *   dx = 3246 - 3232 = +14
 *   dz = 3232 - 3232 = 0
 * 
 *   Check X: -15 ≤ 14 ≤ 14? YES (exactly on edge) ✓
 *   Check Z: -15 ≤ 0 ≤ 14? YES ✓
 * 
 *   Result: true (edge tiles are visible)
 * 
 * MANHATTAN VS EUCLIDEAN DISTANCE:
 * 
 *   Manhattan distance:
 *     - Checks rectangle: |dx| ≤ 15, |dz| ≤ 15
 *     - Fast: 4 comparisons
 *     - What this function uses
 * 
 *   Euclidean distance:
 *     - Checks circle: sqrt(dx² + dz²) ≤ radius
 *     - Slow: multiplication, addition, square root
 *     - More accurate for round viewport
 * 
 *   Performance comparison:
 *     Manhattan: ~4 CPU cycles (comparisons)
 *     Euclidean: ~100+ CPU cycles (sqrt is expensive)
 * 
 *   For 100 NPCs, Manhattan is ~2500× faster!
 * 
 * NETWORK OPTIMIZATION:
 *   This check prevents sending update packets for:
 *     - NPCs too far away
 *     - Other players outside viewport
 *     - Ground items not visible
 * 
 *   Bandwidth savings:
 *     Without culling: Send all 1000 NPCs = ~50 KB/update
 *     With culling:    Send ~50 visible NPCs = ~2.5 KB/update
 *     Savings: 95% bandwidth reduction!
 * 
 * IMPLEMENTATION DETAILS:
 * 
 *   Bounds check uses <= and >= (inclusive):
 *     dx <= 14  (not < 15) for clarity
 *     dx >= -15 (not > -16) for clarity
 * 
 *   Short-circuit evaluation:
 *     If dx fails, dz is not checked (performance)
 *     Most entities fail on X, so this is common
 * 
 * COMPLEXITY: O(1) time (4 comparisons maximum)
 */
bool position_is_viewable_from(const Position* pos, const Position* other) {
    /* Calculate displacement from pos to other */
    i32 dx = other->x - pos->x;
    i32 dz = other->z - pos->z;
    
    /* Check if displacement is within viewport rectangle */
    return dx <= 14 && dx >= -15 && dz <= 14 && dz >= -15;
}

/*******************************************************************************
 * POINT FUNCTIONS - Pathfinding Support
 ******************************************************************************/

/*
 * point_init - Initialize pathfinding point with position and direction
 * 
 * @param point      Point structure to initialize
 * @param x          East/West coordinate
 * @param z          North/South coordinate
 * @param direction  Movement direction (-1=start, 0-7=compass direction)
 * 
 * MEMORY LAYOUT:
 *   ┌────────────┬────────────┬────────────┐
 *   │   x=3232   │   z=3232   │  dir=-1    │  12 bytes (may be 16 with padding)
 *   └────────────┴────────────┴────────────┘
 * 
 * PURPOSE IN PATHFINDING:
 *   During breadth-first search or A*, each explored tile needs:
 *     1. Position (x, z) - where the tile is
 *     2. Direction - how we arrived from parent tile
 * 
 *   This allows path reconstruction by backtracking from goal to start
 * 
 * PATHFINDING ALGORITHM EXAMPLE:
 * 
 *   Starting position: (3232, 3232)
 *   Goal position:     (3235, 3234)
 * 
 *   BFS exploration:
 *     1. Initialize start: point_init(&start, 3232, 3232, -1)
 *        Direction = -1 indicates no parent (starting point)
 * 
 *     2. Explore neighbor northeast: (3233, 3233)
 *        point_init(&point, 3233, 3233, DIR_NE)
 *        Direction = DIR_NE means "came from southwest"
 * 
 *     3. Explore neighbor east: (3233, 3232)
 *        point_init(&point, 3233, 3232, DIR_E)
 *        Direction = DIR_E means "came from west"
 * 
 *     4. Continue until goal reached...
 * 
 * PATH RECONSTRUCTION:
 * 
 *   After finding goal, reconstruct path by following directions backward:
 * 
 *   1. At goal (3235, 3234), direction = DIR_E
 *      → Parent was to the west: (3234, 3234)
 * 
 *   2. At (3234, 3234), direction = DIR_N
 *      → Parent was to the south: (3234, 3233)
 * 
 *   3. At (3234, 3233), direction = DIR_NE
 *      → Parent was to the southwest: (3233, 3232)
 * 
 *   4. At (3233, 3232), direction = DIR_E
 *      → Parent was to the west: (3232, 3232)
 * 
 *   5. At (3232, 3232), direction = -1
 *      → This is the start! Stop.
 * 
 *   Final path: (3232, 3232) → (3233, 3232) → (3233, 3233) → 
 *               (3234, 3234) → (3235, 3234)
 * 
 * DIRECTION INVERSION (for backtracking):
 * 
 *   To get parent from child:
 *     Use negative of direction delta
 * 
 *   Example: If direction = DIR_NE (dx=+1, dz=+1)
 *     Parent is at: (x - 1, z - 1)  [opposite direction]
 * 
 *   Lookup table for parent calculation:
 *     parent_x = x - DIRECTION_DELTA_X[direction]
 *     parent_z = z - DIRECTION_DELTA_Z[direction]
 * 
 * DATA STRUCTURE USAGE:
 * 
 *   Typical pathfinding uses array or hash table:
 *     Point visited[MAX_SIZE];
 *     int visit_count = 0;
 * 
 *   On exploration:
 *     point_init(&visited[visit_count++], new_x, new_z, dir);
 * 
 *   On path reconstruction:
 *     int x = goal_x, z = goal_z;
 *     while (true) {
 *         Point* p = find_point(visited, x, z);
 *         if (p->direction == -1) break;  // Reached start
 *         
 *         // Move to parent
 *         x -= DIRECTION_DELTA_X[p->direction];
 *         z -= DIRECTION_DELTA_Z[p->direction];
 *     }
 * 
 * EXAMPLE USAGE:
 *   Point start_point;
 *   point_init(&start_point, 3232, 3232, -1);
 * 
 *   Point next_point;
 *   point_init(&next_point, 3233, 3233, DIR_NE);
 * 
 * COMPLEXITY: O(1) time, O(1) space
 */
void point_init(Point* point, u32 x, u32 z, i32 direction) {
    /* Direct field assignment */
    point->x = x;
    point->z = z;
    point->direction = direction;
}
