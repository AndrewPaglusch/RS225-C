/*******************************************************************************
 * COLLISION.H - Tile-Based Collision Detection System
 *******************************************************************************
 * 
 * Implements spatial collision detection for the game world using a tile-based
 * grid system with bitflag collision markers. Each tile stores a 32-bit integer
 * containing collision flags for walls, objects, entities, and projectiles.
 * 
 * TILE-BASED COLLISION:
 *   World divided into tiles (typically 1x1 game units)
 *   Each tile has collision flags indicating what blocks movement
 *   Flags combine using bitwise OR for multiple collision types
 * 
 * FLAG STRUCTURE (32 bits):
 *   Bits 0-7:    Directional wall collision (8 directions)
 *   Bit 8:       Object collision
 *   Bits 9-16:   Projectile blockers (walls + object)
 *   Bit 17:      Floor decoration
 *   Bit 18:      NPC occupancy
 *   Bit 19:      Player occupancy
 *   Bit 20:      Floor blocking
 *   Bit 31:      Roof flag (indoor/outdoor)
 * 
 * MULTI-LEVEL SYSTEM:
 *   4 height levels (0-3) stored separately
 *   Level 0: Ground floor
 *   Level 1-2: Upper floors
 *   Level 3: Roof level
 * 
 * MEMORY LAYOUT:
 *   CollisionSystem contains multiple CollisionMaps
 *   Each CollisionMap covers a region (e.g., 64x64 tiles)
 *   Each map has 4 levels
 *   Each level is a 2D array of u32 flags
 * 
 ******************************************************************************/

#ifndef COLLISION_H
#define COLLISION_H

#include "types.h"

/*******************************************************************************
 * COLLISION FLAGS (32-bit Bitmask)
 *******************************************************************************
 * 
 * Each tile stores collision information as bitflags.
 * Multiple flags can be combined using bitwise OR.
 * 
 * WALL DIRECTIONS:
 *   Walls block movement between adjacent tiles
 *   8 directions corresponding to 8-directional movement
 *   
 *   Layout:
 *     NW(0x1)  N(0x2)  NE(0x4)
 *     W(0x80)    *     E(0x8)
 *     SW(0x40) S(0x20) SE(0x10)
 * 
 * CHECKING COLLISION:
 *   if (flags & COLLISION_WALL_NORTH) {
 *       // Cannot move north from this tile
 *   }
 * 
 * ADDING FLAGS:
 *   flags |= COLLISION_WALL_NORTH | COLLISION_OBJECT;
 * 
 * REMOVING FLAGS:
 *   flags &= ~COLLISION_WALL_NORTH;
 * 
 ******************************************************************************/
/* Collision flags - matches RS2 protocol */

typedef enum {
    COLLISION_NONE = 0,
    
    /* Wall collision flags (directional) */
    COLLISION_WALL_NORTH_WEST = 0x1,
    COLLISION_WALL_NORTH = 0x2,
    COLLISION_WALL_NORTH_EAST = 0x4,
    COLLISION_WALL_EAST = 0x8,
    COLLISION_WALL_SOUTH_EAST = 0x10,
    COLLISION_WALL_SOUTH = 0x20,
    COLLISION_WALL_SOUTH_WEST = 0x40,
    COLLISION_WALL_WEST = 0x80,
    
    /* Object collision */
    COLLISION_OBJECT = 0x100,
    
    /* Projectile blockers */
    COLLISION_WALL_NORTH_WEST_PROJ_BLOCKER = 0x200,
    COLLISION_WALL_NORTH_PROJ_BLOCKER = 0x400,
    COLLISION_WALL_NORTH_EAST_PROJ_BLOCKER = 0x800,
    COLLISION_WALL_EAST_PROJ_BLOCKER = 0x1000,
    COLLISION_WALL_SOUTH_EAST_PROJ_BLOCKER = 0x2000,
    COLLISION_WALL_SOUTH_PROJ_BLOCKER = 0x4000,
    COLLISION_WALL_SOUTH_WEST_PROJ_BLOCKER = 0x8000,
    COLLISION_WALL_WEST_PROJ_BLOCKER = 0x10000,
    COLLISION_OBJECT_PROJ_BLOCKER = 0x20000,
    
    /* Floor decorations */
    COLLISION_FLOOR_DECORATION = 0x40000,
    
    /* Entity collision */
    COLLISION_NPC = 0x80000,
    COLLISION_PLAYER = 0x100000,
    
    /* Floor blocking */
    COLLISION_FLOOR = 0x200000,
    
    /* Roof flag (indoor/outdoor) */
    COLLISION_ROOF = 0x80000000,
    
    /* Combined flags */
    COLLISION_WALK_BLOCKED = 0x200000 | 0x40000 | 0x100 | 0xFF,  /* floor + floor_decoration + object + all walls */
} CollisionFlag;

/*******************************************************************************
 * BLOCK WALK TYPES
 *******************************************************************************
 * 
 * Defines which entity types an NPC blocks.
 * Used when adding NPC collision to tiles.
 * 
 * BLOCK_WALK_NONE: NPC does not block movement (decorative)
 * BLOCK_WALK_NPC:  Blocks other NPCs but not players
 * BLOCK_WALK_ALL:  Blocks both NPCs and players
 * 
 ******************************************************************************/
/* Block walk types */

typedef enum {
    BLOCK_WALK_NONE = 0,    /* No collision with entities */
    BLOCK_WALK_NPC = 1,     /* Blocks NPCs only */
    BLOCK_WALK_ALL = 2      /* Blocks both NPCs and players */
} BlockWalk;

/*******************************************************************************
 * MOVEMENT RESTRICTION TYPES
 *******************************************************************************
 * 
 * Defines special movement modes for entities.
 * Used in pathfinding to apply custom collision rules.
 * 
 * MOVE_RESTRICT_NORMAL:        Standard collision checking
 * MOVE_RESTRICT_BLOCKED:       Cannot walk through other entities
 * MOVE_RESTRICT_BLOCKED_NORMAL: Line of sight required
 * MOVE_RESTRICT_INDOORS:       Can only move on indoor tiles (ROOF flag set)
 * MOVE_RESTRICT_OUTDOORS:      Can only move on outdoor tiles (ROOF flag clear)
 * MOVE_RESTRICT_NOMOVE:        Cannot move (frozen/stunned)
 * MOVE_RESTRICT_PASSTHRU:      Ignores entity collision
 * 
 ******************************************************************************/
/* Movement restriction types */

typedef enum {
    MOVE_RESTRICT_NORMAL = 0,        /* Standard collision checking */
    MOVE_RESTRICT_BLOCKED = 1,       /* Can't walk through players */
    MOVE_RESTRICT_BLOCKED_NORMAL = 2,/* Line of sight movement only */
    MOVE_RESTRICT_INDOORS = 3,       /* Can only move indoors */
    MOVE_RESTRICT_OUTDOORS = 4,      /* Can only move outdoors */
    MOVE_RESTRICT_NOMOVE = 5,        /* Cannot move at all */
    MOVE_RESTRICT_PASSTHRU = 6       /* Can pass through entities */
} MoveRestrict;

/*******************************************************************************
 * DATA STRUCTURES
 ******************************************************************************/

/*
 * CollisionLevel - Collision data for single height level
 * 
 * flags:     Heap-allocated array of u32, size = width * height
 * width:     Width in tiles
 * height:    Height in tiles
 * level:     Height level index (0-3)
 * allocated: Whether flags array has been allocated
 * 
 * INDEXING:
 *   index = y * width + x
 *   flags[index] contains collision for tile (x, y)
 */
/* Collision map for a single level */
typedef struct {
    u32* flags;         /* Collision flags array (width * height) */
    u32 width;          /* Map width in tiles */
    u32 height;         /* Map height in tiles */
    u32 level;          /* Height level (0-3) */
    bool allocated;     /* Whether this level is allocated */
} CollisionLevel;

/*
 * CollisionMap - Collision data for a map region
 * 
 * levels:  4 collision levels (ground + 3 upper floors)
 * base_x:  World X coordinate of map origin
 * base_z:  World Z coordinate of map origin
 * 
 * COORDINATE MAPPING:
 *   local_x = world_x - base_x
 *   local_z = world_z - base_z
 */
/* Main collision map structure */
typedef struct {
    CollisionLevel levels[4];   /* 4 height levels */
    u32 base_x;                 /* Base X coordinate for this map section */
    u32 base_z;                 /* Base Z coordinate for this map section */
} CollisionMap;

/*
 * CollisionSystem - Global collision manager
 * 
 * maps:          Dynamic array of collision maps
 * map_count:     Number of currently allocated maps
 * map_capacity:  Capacity of maps array
 * world_width:   Total world width in tiles
 * world_height:  Total world height in tiles
 * 
 * DYNAMIC ALLOCATION:
 *   Maps are allocated on-demand as players explore world
 *   Reduces memory usage for unused regions
 */
/* Collision system */
typedef struct {
    CollisionMap* maps;         /* Array of collision maps */
    u32 map_count;             /* Number of allocated maps */
    u32 map_capacity;          /* Capacity of maps array */
    u32 world_width;           /* World width in tiles */
    u32 world_height;          /* World height in tiles */
} CollisionSystem;

/* Global collision system */
extern CollisionSystem* g_collision;

/* Collision system functions */

/*******************************************************************************
 * LIFECYCLE FUNCTIONS
 ******************************************************************************/
/* Collision system functions */

/*
 * collision_create - Allocate collision system
 * 
 * @param world_width   Total world width in tiles
 * @param world_height  Total world height in tiles
 * @return              Allocated system or NULL on failure
 */
CollisionSystem* collision_create(u32 world_width, u32 world_height);

/*
 * collision_destroy - Free collision system and all maps
 * 
 * @param collision  System to destroy
 */
void collision_destroy(CollisionSystem* collision);

/*
 * collision_init - Initialize collision system
 * 
 * @param collision  System to initialize
 * @return           true on success
 */
bool collision_init(CollisionSystem* collision);

/*******************************************************************************
 * MAP MANAGEMENT
 ******************************************************************************/
/* Collision map allocation */

/*
 * collision_allocate_map - Allocate collision map for region
 * 
 * @param collision  Collision system
 * @param x          Region X coordinate
 * @param z          Region Z coordinate
 * @return           Allocated map or NULL on failure
 */
CollisionMap* collision_allocate_map(CollisionSystem* collision, u32 x, u32 z);

/*
 * collision_get_level - Get collision level for tile
 * 
 * @param collision  Collision system
 * @param x          World X coordinate
 * @param z          World Z coordinate
 * @param level      Height level (0-3)
 * @return           Collision level or NULL if not allocated
 */
CollisionLevel* collision_get_level(CollisionSystem* collision, u32 x, u32 z, u32 level);

/*******************************************************************************
 * FLAG OPERATIONS
 ******************************************************************************/
/* Collision flag management */

/*
 * collision_get_flags - Read collision flags for tile
 * 
 * @param collision  Collision system
 * @param x          World X coordinate
 * @param z          World Z coordinate
 * @param level      Height level
 * @return           Collision flags (0 if tile not allocated)
 */
u32 collision_get_flags(CollisionSystem* collision, u32 x, u32 z, u32 level);

/*
 * collision_set_flags - Overwrite collision flags for tile
 * 
 * @param collision  Collision system
 * @param x          World X coordinate
 * @param z          World Z coordinate
 * @param level      Height level
 * @param flags      New flags value
 */
void collision_set_flags(CollisionSystem* collision, u32 x, u32 z, u32 level, u32 flags);

/*
 * collision_add_flags - Add flags to tile (bitwise OR)
 * 
 * @param collision  Collision system
 * @param x          World X coordinate
 * @param z          World Z coordinate
 * @param level      Height level
 * @param flags      Flags to add
 */
void collision_add_flags(CollisionSystem* collision, u32 x, u32 z, u32 level, u32 flags);

/*
 * collision_remove_flags - Remove flags from tile (bitwise AND NOT)
 * 
 * @param collision  Collision system
 * @param x          World X coordinate
 * @param z          World Z coordinate
 * @param level      Height level
 * @param flags      Flags to remove
 */
void collision_remove_flags(CollisionSystem* collision, u32 x, u32 z, u32 level, u32 flags);

/*******************************************************************************
 * MOVEMENT VALIDATION
 ******************************************************************************/
/* Movement validation */

/*
 * collision_can_move - Check if entity can move in direction
 * 
 * @param collision  Collision system
 * @param x          Current X coordinate
 * @param z          Current Z coordinate
 * @param level      Height level
 * @param dx         X delta (-1, 0, 1)
 * @param dz         Z delta (-1, 0, 1)
 * @param size       Entity size in tiles (1 for normal, 2+ for large NPCs)
 * @param restrict   Movement restriction mode
 * @return           true if movement allowed
 */
bool collision_can_move(CollisionSystem* collision, u32 x, u32 z, u32 level, 
                       i32 dx, i32 dz, u32 size, MoveRestrict restrict);

/*
 * collision_line_of_sight - Check if two tiles have line of sight
 * 
 * @param collision  Collision system
 * @param x1         Start X coordinate
 * @param z1         Start Z coordinate
 * @param x2         End X coordinate
 * @param z2         End Z coordinate
 * @param level      Height level
 * @return           true if line of sight exists
 * 
 * Uses ray-casting algorithm to check for projectile blockers
 */
bool collision_line_of_sight(CollisionSystem* collision, u32 x1, u32 z1, 
                            u32 x2, u32 z2, u32 level);

/*******************************************************************************
 * ENTITY COLLISION
 ******************************************************************************/
/* Entity collision updates */

/*
 * collision_add_player - Mark tiles as occupied by player
 * 
 * @param collision  Collision system
 * @param x          Player X coordinate
 * @param z          Player Z coordinate
 * @param level      Height level
 * @param size       Player size (typically 1)
 */
void collision_add_player(CollisionSystem* collision, u32 x, u32 z, u32 level, u32 size);

/*
 * collision_remove_player - Clear player occupancy flags
 * 
 * @param collision  Collision system
 * @param x          Player X coordinate
 * @param z          Player Z coordinate
 * @param level      Height level
 * @param size       Player size
 */
void collision_remove_player(CollisionSystem* collision, u32 x, u32 z, u32 level, u32 size);

/*
 * collision_add_npc - Mark tiles as occupied by NPC
 * 
 * @param collision  Collision system
 * @param x          NPC X coordinate
 * @param z          NPC Z coordinate
 * @param level      Height level
 * @param size       NPC size (1-5 tiles)
 * @param block      Block type (none/npc/all)
 */
void collision_add_npc(CollisionSystem* collision, u32 x, u32 z, u32 level, u32 size, BlockWalk block);

/*
 * collision_remove_npc - Clear NPC occupancy flags
 * 
 * @param collision  Collision system
 * @param x          NPC X coordinate
 * @param z          NPC Z coordinate
 * @param level      Height level
 * @param size       NPC size
 * @param block      Block type
 */
void collision_remove_npc(CollisionSystem* collision, u32 x, u32 z, u32 level, u32 size, BlockWalk block);

/*******************************************************************************
 * LOCATION COLLISION
 ******************************************************************************/
/* Location collision updates */

/*
 * collision_add_wall - Add wall collision to tile
 * 
 * @param collision  Collision system
 * @param x          Wall X coordinate
 * @param z          Wall Z coordinate
 * @param level      Height level
 * @param type       Wall type (affects which directions blocked)
 * @param rotation   Wall rotation (0-3)
 */
void collision_add_wall(CollisionSystem* collision, u32 x, u32 z, u32 level, u32 type, u32 rotation);

/*
 * collision_remove_wall - Remove wall collision from tile
 * 
 * @param collision  Collision system
 * @param x          Wall X coordinate
 * @param z          Wall Z coordinate
 * @param level      Height level
 * @param type       Wall type
 * @param rotation   Wall rotation
 */
void collision_remove_wall(CollisionSystem* collision, u32 x, u32 z, u32 level, u32 type, u32 rotation);

/*
 * collision_add_object - Add object collision
 * 
 * @param collision  Collision system
 * @param x          Object X coordinate
 * @param z          Object Z coordinate
 * @param level      Height level
 * @param width      Object width in tiles
 * @param length     Object length in tiles
 * @param solid      Whether object blocks movement
 */
void collision_add_object(CollisionSystem* collision, u32 x, u32 z, u32 level, 
                         u32 width, u32 length, bool solid);

/*
 * collision_remove_object - Remove object collision
 * 
 * @param collision  Collision system
 * @param x          Object X coordinate
 * @param z          Object Z coordinate
 * @param level      Height level
 * @param width      Object width
 * @param length     Object length
 * @param solid      Whether object was solid
 */
void collision_remove_object(CollisionSystem* collision, u32 x, u32 z, u32 level, 
                            u32 width, u32 length, bool solid);

/*******************************************************************************
 * DEBUG UTILITIES
 ******************************************************************************/
/* Debug functions */

/*
 * collision_debug_print - Print collision flags around tile
 * 
 * @param collision  Collision system
 * @param x          Center X coordinate
 * @param z          Center Z coordinate
 * @param level      Height level
 * @param radius     Radius around center to print
 */
void collision_debug_print(CollisionSystem* collision, u32 x, u32 z, u32 level, u32 radius);

#endif /* COLLISION_H */
