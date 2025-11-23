/*******************************************************************************
 * POSITION.H - Spatial Coordinate System and Direction Encoding
 *******************************************************************************
 * 
 * EDUCATIONAL PURPOSE:
 * This file demonstrates fundamental concepts in:
 *   - 2D coordinate systems (absolute, region-based, local)
 *   - Spatial partitioning and indexing
 *   - Bit manipulation for fast coordinate conversion
 *   - Direction encoding (8-directional movement)
 *   - Distance calculations and viewport culling
 *   - Memory-efficient position representation
 * 
 * CORE CONCEPT - RUNESCAPE COORDINATE SYSTEM:
 * The game world uses a hierarchical coordinate system with three levels:
 *   1. ABSOLUTE COORDINATES: Global position in entire world (0-16383)
 *   2. REGION COORDINATES: 64x64 tile chunks for network optimization
 *   3. LOCAL COORDINATES: Position relative to region base (0-63)
 * 
 * COORDINATE HIERARCHY:
 * ┌───────────────────────────────────────────────────────────────────────┐
 * │                    WORLD (16384 x 16384 tiles)                        │
 * │  ┌──────────────────────────────────────────────────────────────┐     │
 * │  │               REGION (64 x 64 tiles)                         │     │
 * │  │  ┌────────────────────────────────────────────────┐          │     │
 * │  │  │   LOCAL (coordinate within region)             │          │     │
 * │  │  │                                                │          │     │
 * │  │  │   Example: Absolute (3232, 3232)               │          │     │
 * │  │  │            Region   (50, 50)                   │          │     │
 * │  │  │            Local    (0, 0)                     │          │     │
 * │  │  └────────────────────────────────────────────────┘          │     │
 * │  └──────────────────────────────────────────────────────────────┘     │
 * └───────────────────────────────────────────────────────────────────────┘
 * 
 * COORDINATE TRANSFORMATION PIPELINE:
 * 
 *   ABSOLUTE → REGION:
 *   ┌─────────────────────────────────────────────────────────┐
 *   │  region_x = (absolute_x >> 3) - 6                       │
 *   │  region_z = (absolute_z >> 3) - 6                       │
 *   │                                                         │
 *   │  Explanation:                                           │
 *   │    >> 3  = Divide by 8 (each region is 8x8 chunks)      │
 *   │    - 6   = Offset correction for map origin             │
 *   └─────────────────────────────────────────────────────────┘
 * 
 *   REGION → ABSOLUTE BASE:
 *   ┌─────────────────────────────────────────────────────────┐
 *   │  absolute_base_x = region_x << 3                        │
 *   │  absolute_base_z = region_z << 3                        │
 *   │                                                         │
 *   │  Explanation:                                           │
 *   │    << 3  = Multiply by 8 (inverse of >> 3)              │
 *   └─────────────────────────────────────────────────────────┘
 * 
 *   ABSOLUTE → LOCAL (relative to base):
 *   ┌─────────────────────────────────────────────────────────┐
 *   │  local_x = absolute_x - absolute_base_x                 │
 *   │  local_z = absolute_z - absolute_base_z                 │
 *   │                                                         │
 *   │  Result range: [0, 63] (64 tiles per region side)       │
 *   └─────────────────────────────────────────────────────────┘
 * 
 * VISUAL COORDINATE EXAMPLE:
 * 
 *   Absolute Coordinates (global map):
 *   ┌─────────────────────────────────────────┐
 *   │                                         │  North (increasing Z)
 *   │                  *  (3232, 3240)        │      ↑
 *   │                                         │      │
 *   │           (3200, 3200)                  │      │
 *   │                  +────────→             │      └──→ East (increasing X)
 *   └─────────────────────────────────────────┘
 * 
 *   Region Coordinates (50, 50):
 *   ┌──────────────────────────────────────────────────────────┐
 *   │  Region covers absolute area:                            │
 *   │    X: [3200, 3263]  (64 tiles wide)                      │
 *   │    Z: [3200, 3263]  (64 tiles tall)                      │
 *   │                                                          │
 *   │  Calculation:                                            │
 *   │    region_x = (3232 >> 3) - 6 = 404 - 6 = 398            │
 *   │    But with offset: region_x = 50                        │
 *   └──────────────────────────────────────────────────────────┘
 * 
 *   Local Coordinates within region:
 *   ┌─────────────────────────────────────────┐
 *   │  (0,63)                       (63,63)   │  Local grid
 *   │     +────────────────────────+          │  (64 x 64)
 *   │     │                        │          │
 *   │     │      *  (32, 40)       │          │
 *   │     │                        │          │
 *   │     +────────────────────────+          │
 *   │  (0,0)                        (63,0)    │
 *   └─────────────────────────────────────────┘
 * 
 * DIRECTION ENCODING (8 compass directions):
 * 
 *   Binary encoding uses 3 bits (values 0-7):
 *   ┌─────────────────────────────────────────┐
 *   │        1 (North)                        │
 *   │       ↑                                 │
 *   │  0 ↖  │  ↗ 2                           │
 *   │       │                                 │
 *   │  3 ←──+──→ 4  (directions)              │
 *   │       │                                 │
 *   │  5 ↙  │  ↘ 7                           │
 *   │       ↓                                 │
 *   │        6 (South)                        │
 *   └─────────────────────────────────────────┘
 * 
 *   Encoding: DIR_NW=0, DIR_N=1, DIR_NE=2,
 *             DIR_W=3,  DIR_E=4,
 *             DIR_SW=5, DIR_S=6, DIR_SE=7
 * 
 * DIRECTION DELTAS (movement vectors):
 * 
 *   Direction │  DX  │  DZ  │ Meaning
 *   ──────────┼──────┼──────┼─────────────────────
 *   DIR_NW(0) │  -1  │  +1  │ Move west and north
 *   DIR_N (1) │   0  │  +1  │ Move north
 *   DIR_NE(2) │  +1  │  +1  │ Move east and north
 *   DIR_W (3) │  -1  │   0  │ Move west
 *   DIR_E (4) │  +1  │   0  │ Move east
 *   DIR_SW(5) │  -1  │  -1  │ Move west and south
 *   DIR_S (6) │   0  │  -1  │ Move south
 *   DIR_SE(7) │  +1  │  -1  │ Move east and south
 * 
 * VIEWPORT CULLING:
 * 
 *   Players only see entities within a fixed viewport:
 *   ┌──────────────────────────────────────────────────┐
 *   │              VIEWPORT (30x30 tiles)              │
 *   │  ┌────────────────────────────────────┐          │
 *   │  │     (-15, +14)         (+14, +14)  │          │
 *   │  │         ┌─────────────────┐        │          │
 *   │  │         │                 │        │          │
 *   │  │         │       @ (0,0)   │        │          │
 *   │  │         │     (player)    │        │          │
 *   │  │         └─────────────────┘        │          │
 *   │  │     (-15, -15)         (+14, -15)  │          │
 *   │  └────────────────────────────────────┘          │
 *   │                                                  │
 *   │  Range: X ∈ [-15, 14], Z ∈ [-15, 14]             │
 *   │  Total: 30 x 30 = 900 tiles visible              │
 *   └──────────────────────────────────────────────────┘
 * 
 * BIT MANIPULATION TECHNIQUES:
 * 
 *   1. FAST DIVISION BY 8 (region calculation):
 *      x >> 3  ≡  x / 8  (but faster on most CPUs)
 *      
 *      Example: 3232 >> 3
 *        Binary: 110010100000 >> 3 = 110010100 = 404
 * 
 *   2. FAST MULTIPLICATION BY 8 (inverse operation):
 *      x << 3  ≡  x * 8
 *      
 *      Example: 404 << 3
 *        Binary: 110010100 << 3 = 110010100000 = 3232
 * 
 *   3. MODULO 8 USING BIT MASK:
 *      x & 7  ≡  x % 8
 *      
 *      Example: 3237 & 7
 *        Binary: 110010100101 & 111 = 101 = 5
 *        Proof:  3237 = 404*8 + 5, so 3237 % 8 = 5
 * 
 * MEMORY LAYOUT:
 * 
 *   Position Structure (12 bytes on 32-bit, 12 bytes on 64-bit):
 *   ┌────────────┬────────────┬────────────┐
 *   │     x      │     z      │   height   │
 *   │  (4 bytes) │  (4 bytes) │  (4 bytes) │
 *   └────────────┴────────────┴────────────┘
 *     u32 (0-16383) u32 (0-16383) u32 (0-3)
 * 
 *   Point Structure (12 bytes on 32-bit, 16 bytes on 64-bit):
 *   ┌────────────┬────────────┬────────────┐
 *   │     x      │     z      │ direction  │
 *   │  (4 bytes) │  (4 bytes) │  (4 bytes) │
 *   └────────────┴────────────┴────────────┘
 *     u32           u32          i32 (-1 or 0-7)
 * 
 * USAGE PATTERN:
 * 
 *   1. Create position:
 *      Position pos;
 *      position_init(&pos, 3232, 3232, 0);
 * 
 *   2. Move by delta:
 *      position_move(&pos, 5, -3);  // Move 5 east, 3 south
 * 
 *   3. Get region coordinates:
 *      u32 region_x = position_get_region_x(&pos);
 *      u32 region_z = position_get_region_z(&pos);
 * 
 *   4. Get local coordinates (relative to base):
 *      Position base;
 *      position_init(&base, 3200, 3200, 0);
 *      u32 local_x = position_get_local_x(&pos, &base);
 *      u32 local_z = position_get_local_z(&pos, &base);
 * 
 *   5. Calculate direction from deltas:
 *      i32 dir = position_direction(1, 1);  // Returns DIR_NE (2)
 * 
 *   6. Check if position is visible from another:
 *      bool visible = position_is_viewable_from(&pos, &other_pos);
 * 
 ******************************************************************************/

#ifndef POSITION_H
#define POSITION_H

#include "types.h"

/*******************************************************************************
 * POSITION - Absolute 3D Coordinate in Game World
 *******************************************************************************
 * 
 * FIELDS:
 *   x:       East/West coordinate (0-16383 for full map)
 *   z:       North/South coordinate (0-16383 for full map)
 *   height:  Plane/floor level (0-3, where 0=ground, 1-3=upper floors)
 * 
 * COORDINATE NAMING:
 *   - X axis: West (-) to East (+)
 *   - Z axis: South (-) to North (+)  [called Y in some implementations]
 *   - Height: Vertical layers (dungeons use 0, ground floor uses 0-1)
 * 
 * WHY Z INSTEAD OF Y?
 *   RuneScape uses X-Z for horizontal plane (common in 3D graphics)
 *   Y is typically "up" in 3D space, but here it's encoded as "height"
 * 
 * COORDINATE RANGES:
 *   - Normal game world: X,Z ∈ [2944, 3392] (448x448 tiles)
 *   - Full addressable space: X,Z ∈ [0, 16383] (16K x 16K tiles)
 *   - Height levels: 0=ground, 1-3=upper floors
 * 
 * INVARIANTS:
 *   - x, z should be < 16384 (14-bit values)
 *   - height should be 0-3 (2-bit value)
 * 
 * COMPLEXITY: O(1) storage (12 bytes)
 ******************************************************************************/
typedef struct {
    u32 x;      /* East/West coordinate (horizontal) */
    u32 z;      /* North/South coordinate (horizontal, often called Y) */
    u32 height; /* Plane level (vertical: 0=ground, 1-3=upper floors) */
} Position;

/*******************************************************************************
 * POINT - Position with Direction for Pathfinding
 *******************************************************************************
 * 
 * FIELDS:
 *   x:          East/West coordinate
 *   z:          North/South coordinate
 *   direction:  Movement direction (-1 = no movement, 0-7 = compass direction)
 * 
 * PURPOSE:
 *   Used in pathfinding to track both position and the direction taken
 *   to reach that position (for path reconstruction)
 * 
 * DIRECTION VALUES:
 *   -1: No movement (destination reached)
 *    0-7: One of 8 compass directions (see DIR_* constants)
 * 
 * PATHFINDING USAGE:
 *   During A* or breadth-first search, each explored tile stores:
 *   - Its coordinates (x, z)
 *   - The direction from parent tile (for path reconstruction)
 * 
 * EXAMPLE PATH RECONSTRUCTION:
 *   1. Start at destination, read direction
 *   2. Follow reverse direction to parent
 *   3. Repeat until reaching start (direction = -1)
 * 
 * COMPLEXITY: O(1) storage (12-16 bytes depending on padding)
 ******************************************************************************/
typedef struct {
    u32 x;          /* East/West coordinate */
    u32 z;          /* North/South coordinate */
    i32 direction;  /* Movement direction: -1=none, 0-7=compass direction */
} Point;

/*******************************************************************************
 * DIRECTION CONSTANTS - 8-Way Compass Encoding
 *******************************************************************************
 * 
 * ENCODING SCHEME:
 *   3-bit encoding (values 0-7) for 8 cardinal and intercardinal directions
 * 
 * VISUAL LAYOUT:
 *          N (1)
 *           ↑
 *    NW(0) ╱│╲ NE(2)
 *         ╱ │ ╲
 *   W(3)─┼──+──┼─ E(4)
 *         ╲ │ ╱
 *    SW(5) ╲│╱ SE(7)
 *           ↓
 *          S (6)
 * 
 * BIT PATTERN ANALYSIS:
 *   Bit 2: East/West component  (0=West, 1=East)
 *   Bit 1: North/South component
 *   Bit 0: Diagonal flag
 * 
 *   DIR_NW = 0b000 = 0: Northwest diagonal
 *   DIR_N  = 0b001 = 1: North cardinal
 *   DIR_NE = 0b010 = 2: Northeast diagonal
 *   DIR_W  = 0b011 = 3: West cardinal
 *   DIR_E  = 0b100 = 4: East cardinal
 *   DIR_SW = 0b101 = 5: Southwest diagonal
 *   DIR_S  = 0b110 = 6: South cardinal
 *   DIR_SE = 0b111 = 7: Southeast diagonal
 * 
 * PROTOCOL EFFICIENCY:
 *   Using 3 bits instead of full byte saves 5 bits per direction
 *   In movement packets with 100+ NPCs, this saves ~63 bytes per packet
 * 
 ******************************************************************************/
enum {
    DIR_NW = 0,  /* Northwest: dx=-1, dz=+1 */
    DIR_N  = 1,  /* North:     dx= 0, dz=+1 */
    DIR_NE = 2,  /* Northeast: dx=+1, dz=+1 */
    DIR_W  = 3,  /* West:      dx=-1, dz= 0 */
    DIR_E  = 4,  /* East:      dx=+1, dz= 0 */
    DIR_SW = 5,  /* Southwest: dx=-1, dz=-1 */
    DIR_S  = 6,  /* South:     dx= 0, dz=-1 */
    DIR_SE = 7   /* Southeast: dx=+1, dz=-1 */
};

/*******************************************************************************
 * DIRECTION DELTA ARRAYS - Movement Vectors
 *******************************************************************************
 * 
 * PURPOSE:
 *   Convert direction constant (0-7) to X/Z displacement
 * 
 * USAGE:
 *   int dir = DIR_NE;  // Northeast
 *   int new_x = old_x + DIRECTION_DELTA_X[dir];  // old_x + 1
 *   int new_z = old_z + DIRECTION_DELTA_Z[dir];  // old_z + 1
 * 
 * ARRAY VALUES:
 *   Index │ Direction │ DELTA_X │ DELTA_Z │ Meaning
 *   ──────┼───────────┼─────────┼─────────┼────────────────────
 *     0   │   NW      │   -1    │   +1    │ Move west and north
 *     1   │   N       │    0    │   +1    │ Move north
 *     2   │   NE      │   +1    │   +1    │ Move east and north
 *     3   │   W       │   -1    │    0    │ Move west
 *     4   │   E       │   +1    │    0    │ Move east
 *     5   │   SW      │   -1    │   -1    │ Move west and south
 *     6   │   S       │    0    │   -1    │ Move south
 *     7   │   SE      │   +1    │   -1    │ Move east and south
 * 
 * EXAMPLE:
 *   Player at (3232, 3232) moves northeast:
 *   
 *   int dir = DIR_NE;  // dir = 2
 *   int dx = DIRECTION_DELTA_X[2];  // dx = +1
 *   int dz = DIRECTION_DELTA_Z[2];  // dz = +1
 *   
 *   New position: (3232 + 1, 3232 + 1) = (3233, 3233)
 * 
 * MOVEMENT VALIDATION:
 *   Before applying delta, check:
 *   - Collision detection (is destination walkable?)
 *   - Bounds checking (is destination in valid map area?)
 *   - Height matching (same floor level?)
 * 
 ******************************************************************************/
static const i32 DIRECTION_DELTA_X[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
static const i32 DIRECTION_DELTA_Z[8] = { 1, 1, 1,  0, 0, -1,-1,-1};

/*******************************************************************************
 * POSITION FUNCTIONS
 ******************************************************************************/

/*
 * position_init - Initialize position structure with coordinates
 * 
 * @param pos     Pointer to position to initialize
 * @param x       East/West coordinate (0-16383)
 * @param z       North/South coordinate (0-16383)
 * @param height  Plane level (0-3)
 * 
 * EXAMPLE:
 *   Position lumbridge;
 *   position_init(&lumbridge, 3222, 3218, 0);  // Lumbridge castle
 * 
 * COMPLEXITY: O(1) time
 */
void position_init(Position* pos, u32 x, u32 z, u32 height);

/*
 * position_move - Apply relative movement to position
 * 
 * @param pos  Position to modify
 * @param dx   X displacement (negative=west, positive=east)
 * @param dz   Z displacement (negative=south, positive=north)
 * 
 * EFFECTS:
 *   pos->x += dx
 *   pos->z += dz
 * 
 * WARNING: Does not perform bounds checking or collision detection!
 * 
 * EXAMPLE:
 *   Position pos;
 *   position_init(&pos, 3200, 3200, 0);
 *   position_move(&pos, 5, -3);  // Move 5 east, 3 south
 *   // Result: pos = (3205, 3197, 0)
 * 
 * COMPLEXITY: O(1) time
 */
void position_move(Position* pos, i32 dx, i32 dz);

/*
 * position_get_region_x - Convert absolute X to region X coordinate
 * 
 * @param pos  Position to query
 * @return     Region X coordinate
 * 
 * ALGORITHM:
 *   region_x = (pos->x >> 3) - 6
 * 
 * EXPLANATION:
 *   1. Divide X by 8 using right shift (>> 3)
 *      Each region is 8x8 chunks, and each chunk is 8x8 tiles
 *      So 8 tiles = 1 chunk, 8 chunks = 1 region
 *      Therefore 64 tiles = 1 region (8*8)
 *   
 *   2. Subtract 6 for map origin offset
 *      RuneScape's world origin is offset by 6 regions
 * 
 * EXAMPLE:
 *   Absolute X = 3232
 *   
 *   Step 1: 3232 >> 3
 *     Binary: 110010100000 >> 3 = 110010100 = 404
 *   
 *   Step 2: 404 - 6 = 398
 *   
 *   Result: region_x = 398
 * 
 * BIT MANIPULATION:
 *   >> 3 is equivalent to / 8 but faster:
 *   - Division requires ~20-40 CPU cycles
 *   - Bit shift requires ~1 CPU cycle
 * 
 * COMPLEXITY: O(1) time
 */
u32 position_get_zone_x(const Position* pos);
u32 position_get_zone_center_x(const Position* pos);
u32 position_get_mapsquare_x(const Position* pos);

/*
 * position_get_region_z - Convert absolute Z to region Z coordinate
 * 
 * @param pos  Position to query
 * @return     Region Z coordinate
 * 
 * ALGORITHM:
 *   region_z = (pos->z >> 3) - 6
 * 
 * EXAMPLE:
 *   Absolute Z = 3200
 *   
 *   Step 1: 3200 >> 3 = 400
 *   Step 2: 400 - 6 = 394
 *   
 *   Result: region_z = 394
 * 
 * COMPLEXITY: O(1) time
 */
u32 position_get_zone_z(const Position* pos);
u32 position_get_zone_center_z(const Position* pos);
u32 position_get_mapsquare_z(const Position* pos);

/*
 * position_get_local_x - Get local X coordinate relative to base position
 * 
 * @param pos   Position to convert
 * @param base  Base position (usually player position or region corner)
 * @return      Local X offset (0-63 typically)
 * 
 * ALGORITHM:
 *   1. Calculate base's region: region_x = (base->x >> 3) - 6
 *   2. Convert region back to absolute: region_base_x = region_x << 3
 *   3. Subtract base from pos: local_x = pos->x - region_base_x
 * 
 * PURPOSE:
 *   Network protocol sends positions relative to player's region
 *   to save bandwidth (6 bits instead of 14 bits per coordinate)
 * 
 * EXAMPLE:
 *   Player position (base): (3232, 3232)
 *   NPC position (pos):     (3240, 3235)
 * 
 *   Step 1: base region_x = (3232 >> 3) - 6 = 404 - 6 = 398
 *   Step 2: region_base_x = 398 << 3 = 3184
 *   Step 3: local_x = 3240 - 3184 = 56
 * 
 *   Result: NPC is at local X offset 56 (within 0-63 range)
 * 
 * RANGE:
 *   Typically [0, 63] for entities in same region
 *   Can exceed 63 if entities are in different regions
 * 
 * COMPLEXITY: O(1) time
 */
u32 position_get_local_x(const Position* pos, const Position* base);

/*
 * position_get_local_z - Get local Z coordinate relative to base position
 * 
 * @param pos   Position to convert
 * @param base  Base position (usually player position or region corner)
 * @return      Local Z offset (0-63 typically)
 * 
 * ALGORITHM: Same as position_get_local_x but for Z axis
 * 
 * EXAMPLE:
 *   Player position (base): (3232, 3232)
 *   NPC position (pos):     (3240, 3235)
 * 
 *   Step 1: base region_z = (3232 >> 3) - 6 = 398
 *   Step 2: region_base_z = 398 << 3 = 3184
 *   Step 3: local_z = 3235 - 3184 = 51
 * 
 *   Result: NPC is at local Z offset 51
 * 
 * COMPLEXITY: O(1) time
 */
u32 position_get_local_z(const Position* pos, const Position* base);

/*
 * position_direction - Calculate compass direction from X/Z deltas
 * 
 * @param dx  X displacement (-1, 0, or +1)
 * @param dz  Z displacement (-1, 0, or +1)
 * @return    Direction constant (0-7), or -1 if no movement
 * 
 * ALGORITHM:
 *   Lookup table based on dx and dz signs
 * 
 * DECISION TREE:
 *   if (dx < 0) {          // Moving west
 *       if (dz < 0) return DIR_SW;   // Southwest
 *       if (dz > 0) return DIR_NW;   // Northwest
 *       return DIR_W;                // West
 *   } else if (dx > 0) {   // Moving east
 *       if (dz < 0) return DIR_SE;   // Southeast
 *       if (dz > 0) return DIR_NE;   // Northeast
 *       return DIR_E;                // East
 *   } else {               // No horizontal movement
 *       if (dz < 0) return DIR_S;    // South
 *       if (dz > 0) return DIR_N;    // North
 *       return -1;                   // No movement
 *   }
 * 
 * TRUTH TABLE:
 *   DX  │ DZ  │ Direction │ Value
 *   ────┼─────┼───────────┼──────
 *   -1  │ +1  │   NW      │   0
 *    0  │ +1  │   N       │   1
 *   +1  │ +1  │   NE      │   2
 *   -1  │  0  │   W       │   3
 *   +1  │  0  │   E       │   4
 *   -1  │ -1  │   SW      │   5
 *    0  │ -1  │   S       │   6
 *   +1  │ -1  │   SE      │   7
 *    0  │  0  │  (none)   │  -1
 * 
 * EXAMPLE:
 *   Player moves from (3232, 3232) to (3233, 3233)
 *   
 *   dx = 3233 - 3232 = +1
 *   dz = 3233 - 3232 = +1
 *   
 *   direction = position_direction(1, 1)
 *   
 *   Logic: dx > 0 and dz > 0
 *   Result: DIR_NE (2)
 * 
 * USAGE IN PROTOCOL:
 *   Movement packets encode direction as 3-bit value (0-7)
 *   -1 (no movement) is typically encoded separately
 * 
 * COMPLEXITY: O(1) time (constant comparisons)
 */
i32 position_direction(i32 dx, i32 dz);

/*
 * position_is_viewable_from - Check if position is within viewport range
 * 
 * @param pos    Position to check (e.g., NPC location)
 * @param other  Observer position (e.g., player location)
 * @return       true if pos is visible from other, false otherwise
 * 
 * ALGORITHM:
 *   Calculate delta: (dx, dz) = (other - pos)
 *   Check if delta is within viewport rectangle:
 *     -15 <= dx <= 14  AND  -15 <= dz <= 14
 * 
 * VIEWPORT DIMENSIONS:
 *   RuneScape uses asymmetric viewport of 30x30 tiles:
 *   - West/South: -15 tiles (15 tiles behind player)
 *   - East/North: +14 tiles (14 tiles ahead of player)
 *   - Total: 30 tiles (15 + 1 + 14)
 * 
 * VISUAL DIAGRAM:
 *   ┌────────────────────────────────┐
 *   │  (-15, +14)        (+14, +14)  │  Visible area
 *   │      ┌──────────────────┐      │
 *   │      │                  │      │
 *   │      │                  │      │
 *   │      │     @ (0, 0)     │      │  @ = observer
 *   │      │    (observer)    │      │
 *   │      │                  │      │
 *   │      └──────────────────┘      │
 *   │  (-15, -15)        (+14, -15)  │
 *   └────────────────────────────────┘
 * 
 * EXAMPLE:
 *   Observer at (3232, 3232)
 *   Target at (3240, 3235)
 * 
 *   dx = 3240 - 3232 = +8
 *   dz = 3235 - 3232 = +3
 * 
 *   Check: -15 <= 8 <= 14? YES
 *          -15 <= 3 <= 14? YES
 *   
 *   Result: true (target is visible)
 * 
 * EXAMPLE (out of range):
 *   Observer at (3232, 3232)
 *   Target at (3250, 3232)  // 18 tiles east
 * 
 *   dx = 3250 - 3232 = +18
 *   dz = 3232 - 3232 = 0
 * 
 *   Check: -15 <= 18 <= 14? NO (18 > 14)
 *   
 *   Result: false (target is too far east)
 * 
 * OPTIMIZATION:
 *   This is a rectangular (Manhattan) distance check, not Euclidean
 *   Avoids expensive sqrt() calculation
 *   Uses simple integer comparisons (very fast)
 * 
 * USAGE:
 *   Filter NPCs/players before sending in update packets
 *   Only send data for entities within viewport
 *   Saves bandwidth and client processing time
 * 
 * COMPLEXITY: O(1) time
 */
bool position_is_viewable_from(const Position* pos, const Position* other);

/*******************************************************************************
 * POINT FUNCTIONS
 ******************************************************************************/

/*
 * point_init - Initialize pathfinding point with position and direction
 * 
 * @param point      Pointer to point to initialize
 * @param x          East/West coordinate
 * @param z          North/South coordinate
 * @param direction  Movement direction (-1 or 0-7)
 * 
 * PURPOSE:
 *   Used in pathfinding algorithms (A*, BFS) to track:
 *   - Current position (x, z)
 *   - Direction taken from parent node (for path reconstruction)
 * 
 * EXAMPLE:
 *   Point start;
 *   point_init(&start, 3232, 3232, -1);  // Starting point (no parent)
 *   
 *   Point next;
 *   point_init(&next, 3233, 3233, DIR_NE);  // Moved northeast from start
 * 
 * PATHFINDING USAGE:
 *   1. Initialize start point with direction = -1
 *   2. For each explored neighbor, create point with:
 *      - Neighbor coordinates
 *      - Direction from current to neighbor
 *   3. To reconstruct path:
 *      - Start at destination
 *      - Follow reverse direction to parent
 *      - Repeat until direction = -1 (reached start)
 * 
 * COMPLEXITY: O(1) time
 */
void point_init(Point* point, u32 x, u32 z, i32 direction);

#endif /* POSITION_H */
