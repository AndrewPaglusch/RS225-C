/*******************************************************************************
 * OBJECT.H - Game Object System and Definition Management
 *******************************************************************************
 * 
 * EDUCATIONAL PURPOSE:
 * This file demonstrates fundamental concepts in:
 *   - Entity-Component-System (ECS) patterns in game development
 *   - Static vs dynamic object instances (definition vs instance)
 *   - Spatial object management and collision detection
 *   - Memory-efficient object representation
 *   - Object orientation and rotation in 3D space
 *   - Interactive object state machines
 *   - Region-based spatial partitioning
 * 
 * CORE CONCEPT - OBJECT SYSTEM ARCHITECTURE:
 * 
 * The game world contains two types of object data:
 *   1. OBJECT DEFINITIONS: Static templates (read from cache, shared)
 *   2. OBJECT INSTANCES: Dynamic state (position, rotation, temporary)
 * 
 * This separation follows the FLYWEIGHT PATTERN to save memory:
 * 
 *   DEFINITION (ObjectDefinition) - Shared immutable data:
 *     - Name, examine text, actions
 *     - Model IDs for rendering
 *     - Physical properties (size, solid, clipped)
 *     - Loaded once from cache, referenced by all instances
 * 
 *   INSTANCE (GameObject) - Per-object mutable state:
 *     - Position in world (x, z, height)
 *     - Rotation (0-3 for 4 cardinal directions)
 *     - Temporary flag (respawns after despawn?)
 *     - Spawn time (for temporary objects)
 * 
 * MEMORY SAVINGS EXAMPLE:
 * 
 *   Without separation (1000 tree objects):
 *     Each tree stores: name="Tree" (64B), examine="A tree" (128B),
 *                       actions (5*32B), models (20B), properties (10B)
 *     Total per tree: ~350 bytes
 *     1000 trees: 350,000 bytes (341 KB)
 * 
 *   With definition/instance separation:
 *     Definition (shared): 350 bytes (stored once)
 *     Instance (each): id=2B, position=12B, rotation=1B, flags=10B = 25 bytes
 *     1000 trees: 350 + (1000 * 25) = 25,350 bytes (24.8 KB)
 *     
 *     SAVINGS: 93% memory reduction!
 * 
 * OBJECT TYPE HIERARCHY:
 * 
 * RuneScape objects are categorized by their spatial properties:
 * 
 *   ┌────────────────────────────────────────────────────────────────┐
 *   │                    OBJECT TYPE CLASSIFICATION                  │
 *   ├────────────────────────────────────────────────────────────────┤
 *   │                                                                │
 *   │  Type 0: WALL (doors, fences, barriers)                        │
 *   │    - Occupies edge between two tiles                           │
 *   │    - Blocks movement in one direction                          │
 *   │    - Example: door at (x,z) facing NORTH blocks (x,z)->(x,z+1) │
 *   │    - Rotation determines which edge (N/E/S/W)                  │
 *   │                                                                │
 *   │  Type 1: WALL_DECORATION (torches, paintings, signs)           │
 *   │    - Attached to wall edge (like Type 0)                       │
 *   │    - Does NOT block movement (purely visual)                   │
 *   │    - Example: torch on north wall of tile (x,z)                │
 *   │                                                                │
 *   │  Type 2: INTERACTABLE (furniture, trees, rocks)                │
 *   │    - Occupies entire tile(s)                                   │
 *   │    - Can have variable size (width x length)                   │
 *   │    - Example: 2x2 tree at (x,z) blocks tiles (x,z) to (x+1,z+1)│
 *   │    - Can be interacted with (chop, mine, open, etc.)           │
 *   │                                                                │
 *   │  Type 3: GROUND_DECORATION (flowers, stones, grass)            │
 *   │    - Occupies tile but does NOT block movement                 │
 *   │    - Purely cosmetic                                           │
 *   │    - Example: flowers at (x,z) can be walked over              │
 *   │                                                                │
 *   └────────────────────────────────────────────────────────────────┘
 * 
 * VISUAL REPRESENTATION OF OBJECT TYPES:
 * 
 *   Type 0 (WALL):              Type 1 (WALL_DECORATION):
 *   ┌──────┬──────┐             ┌──────┬──────┐
 *   │      │      │             │      │      │
 *   │  A   │  B   │             │  A   *  B   │  * = torch (decoration)
 *   │      ▓      │  ▓ = door   │      │      │      (no collision)
 *   ├──────┼──────┤             ├──────┼──────┤
 *   │      │      │             │      │      │
 *   │  C   │  D   │             │  C   │  D   │
 *   └──────┴──────┘             └──────┴──────┘
 * 
 *   Type 2 (INTERACTABLE):      Type 3 (GROUND_DECORATION):
 *   ┌──────┬──────┐             ┌──────┬──────┐
 *   │ TREE │ TREE │             │  .*  │      │  .* = flowers
 *   │ (1x2)│      │             │      │  .*  │      (walkable)
 *   ├──────┼──────┤             ├──────┼──────┤
 *   │      │      │             │  .*  │  .*  │
 *   │      │      │             │      │      │
 *   └──────┴──────┘             └──────┴──────┘
 * 
 * OBJECT ROTATION SYSTEM:
 * 
 * Objects can face 4 cardinal directions (90-degree increments):
 * 
 *   ROTATION VALUE │ DIRECTION │ DEGREES │ VISUAL
 *   ───────────────┼───────────┼─────────┼─────────────────
 *        0         │   WEST    │   0°    │  ←  (facing left)
 *        1         │   NORTH   │   90°   │  ↑  (facing up)
 *        2         │   EAST    │  180°   │  →  (facing right)
 *        3         │   SOUTH   │  270°   │  ↓  (facing down)
 * 
 * ROTATION EFFECTS ON DIFFERENT OBJECT TYPES:
 * 
 *   WALL OBJECTS (Type 0):
 *     Rotation determines which tile edge the wall occupies
 * 
 *     Rotation 0 (WEST):    Rotation 1 (NORTH):
 *     ┌──────┬──────┐       ┌──────┬──────┐
 *     │      │      │       │      │      │
 *     │  ▓   │      │       │    ══════   │  ═ = wall
 *     │      │      │       │      │      │
 *     └──────┴──────┘       └──────┴──────┘
 *     ▓ = wall on west edge   wall on north edge
 * 
 *   INTERACTABLE OBJECTS (Type 2):
 *     Rotation affects model orientation and collision shape
 * 
 *     2x1 Table (rotation 0):     2x1 Table (rotation 1):
 *     ┌────────────┐               ┌──────┐
 *     │   TABLE    │               │TABLE │
 *     │  (2 wide)  │               │ (1w) │
 *     └────────────┘               │  x   │
 *                                  │ (2h) │
 *                                  └──────┘
 * 
 * OBJECT SIZE AND COLLISION:
 * 
 * Objects have width and length defining their footprint:
 * 
 *   width:  Size along X axis (east-west)
 *   length: Size along Z axis (north-south)
 * 
 *   1x1 Object (most common):    2x2 Object (large):
 *   ┌──────┐                      ┌──────┬──────┐
 *   │  ██  │                      │  ██  │  ██  │
 *   └──────┘                      │  ██  │  ██  │
 *                                 ├──────┼──────┤
 *                                 │  ██  │  ██  │
 *                                 │  ██  │  ██  │
 *                                 └──────┴──────┘
 * 
 * COLLISION DETECTION:
 * 
 * Objects affect pathfinding through several properties:
 * 
 *   solid:        Blocks movement (cannot walk through)
 *   impenetrable: Blocks projectiles (arrows, spells)
 *   clipped:      Affects client-side collision (visual blocking)
 * 
 * EXAMPLE COLLISION SCENARIOS:
 * 
 *   Scenario 1: Walking through objects
 *     Tree (solid=true)      →  Player CANNOT walk through
 *     Flowers (solid=false)  →  Player CAN walk through
 * 
 *   Scenario 2: Ranged combat
 *     Wall (impenetrable=true)  →  Arrows BLOCKED
 *     Bush (impenetrable=false) →  Arrows PASS THROUGH
 * 
 *   Scenario 3: Client rendering
 *     Door (clipped=true)   →  Camera stops at door
 *     Torch (clipped=false) →  Camera passes through
 * 
 * INTERACTIVE OBJECTS:
 * 
 * Objects can have up to 5 possible actions (right-click menu):
 * 
 *   DOOR EXAMPLE:
 *     actions[0] = "Open"    (primary action, left-click)
 *     actions[1] = "Knock"   (secondary)
 *     actions[2] = "Pick-lock" (if thieving level sufficient)
 *     actions[3] = ""        (unused)
 *     actions[4] = "Examine" (shows examine text)
 * 
 *   TREE EXAMPLE:
 *     actions[0] = "Chop down"
 *     actions[1] = "Examine"
 *     actions[2-4] = ""
 * 
 * ACTION PRIORITY:
 *   - actions[0] is PRIMARY (triggered by left-click)
 *   - actions[1-4] appear in right-click menu
 *   - Empty strings ("") indicate no action in that slot
 * 
 * TEMPORARY OBJECTS:
 * 
 * Some objects are temporary (despawn after timeout):
 * 
 *   EXAMPLE: Tree stump after chopping
 *     1. Player chops tree at (3232, 3232)
 *     2. Server despawns tree object
 *     3. Server spawns stump object (temporary=true, spawn_time=current_tick)
 *     4. After 60 seconds (100 ticks), stump despawns
 *     5. Server respawns original tree
 * 
 * PERMANENT vs TEMPORARY WORKFLOW:
 * 
 *   Permanent object (loaded from map cache):
 *     - Exists at server startup
 *     - Never despawns
 *     - Stored in region files
 * 
 *   Temporary object (spawned at runtime):
 *     - Created during gameplay
 *     - Has spawn_time timestamp
 *     - Automatically despawns after timeout
 *     - NOT saved to database (regenerated)
 * 
 * OBJECT LIFECYCLE:
 * 
 *   1. DEFINITION LOADING (server startup):
 *      ┌─────────────────────────────────────────────┐
 *      │ Load object definitions from cache          │
 *      │   - Read 30,000+ object templates           │
 *      │   - Parse properties, models, actions       │
 *      │   - Store in definitions array              │
 *      └─────────────────────────────────────────────┘
 * 
 *   2. INSTANCE SPAWNING (region loading):
 *      ┌─────────────────────────────────────────────┐
 *      │ Load region map data                        │
 *      │   - Read object instances for region        │
 *      │   - Create GameObject for each object       │
 *      │   - Link to definition by ID                │
 *      └─────────────────────────────────────────────┘
 * 
 *   3. RUNTIME INTERACTION (player action):
 *      ┌─────────────────────────────────────────────┐
 *      │ Player clicks object                        │
 *      │   - Find GameObject at clicked position     │
 *      │   - Lookup ObjectDefinition by ID           │
 *      │   - Execute action handler                  │
 *      │   - Update object state (open door, etc.)   │
 *      └─────────────────────────────────────────────┘
 * 
 *   4. TEMPORARY OBJECT CLEANUP (periodic tick):
 *      ┌─────────────────────────────────────────────┐
 *      │ Every game tick (600ms)                     │
 *      │   - Check all temporary objects             │
 *      │   - If (current_time - spawn_time) > timeout│
 *      │   - Despawn expired objects                 │
 *      │   - Respawn original objects if needed      │
 *      └─────────────────────────────────────────────┘
 * 
 * SPATIAL INDEXING:
 * 
 * Objects are indexed by region for efficient lookup:
 * 
 *   NAIVE APPROACH (linear search):
 *     To find object at (3232, 3232):
 *       - Search ALL objects in world
 *       - O(n) time where n = total objects (millions!)
 * 
 *   OPTIMIZED APPROACH (region-based):
 *     To find object at (3232, 3232):
 *       1. Calculate region: (3232 >> 3) = region 404
 *       2. Search only objects in region 404
 *       3. O(m) time where m = objects in region (hundreds)
 *       4. Speedup: ~10,000x faster!
 * 
 * OBJECT LOOKUP ALGORITHM:
 * 
 *   function object_get_at(x, z, height, type):
 *     1. Calculate region coordinates from (x, z)
 *     2. Get object list for that region
 *     3. For each object in region:
 *          - Check if position matches (x, z, height)
 *          - Check if type matches (walls vs interactables)
 *          - Return first match
 *     4. Return NULL if no match found
 * 
 * MEMORY LAYOUT:
 * 
 *   ObjectDefinition Structure (approximately 350 bytes):
 *   ┌──────────────┬────────┬──────────────────────────────┐
 *   │ Field        │ Size   │ Purpose                      │
 *   ├──────────────┼────────┼──────────────────────────────┤
 *   │ id           │ 2 B    │ Unique identifier (0-65535)  │
 *   │ name         │ 64 B   │ Display name "Door"          │
 *   │ examine      │ 128 B  │ Examine text "A wooden door" │
 *   │ type         │ 1 B    │ Object type (0-3)            │
 *   │ width        │ 1 B    │ Width in tiles (1-10)        │
 *   │ length       │ 1 B    │ Length in tiles (1-10)       │
 *   │ solid        │ 1 B    │ Blocks movement (bool)       │
 *   │ impenetrable │ 1 B    │ Blocks projectiles (bool)    │
 *   │ interactive  │ 1 B    │ Can be clicked (bool)        │
 *   │ clipped      │ 1 B    │ Client collision (bool)      │
 *   │ model_ids    │ 20 B   │ 3D model IDs (10 * u16)      │
 *   │ model_types  │ 20 B   │ Model types (10 * u16)       │
 *   │ model_count  │ 1 B    │ Number of models (0-10)      │
 *   │ actions      │ 160 B  │ Action strings (5 * 32 B)    │
 *   └──────────────┴────────┴──────────────────────────────┘
 *   TOTAL: ~350 bytes (with padding)
 * 
 *   GameObject Structure (25 bytes):
 *   ┌──────────────┬────────┬──────────────────────────────┐
 *   │ Field        │ Size   │ Purpose                      │
 *   ├──────────────┼────────┼──────────────────────────────┤
 *   │ id           │ 2 B    │ Object definition ID         │
 *   │ position.x   │ 4 B    │ World X coordinate           │
 *   │ position.z   │ 4 B    │ World Z coordinate           │
 *   │ position.h   │ 4 B    │ Height level (0-3)           │
 *   │ type         │ 1 B    │ Object type (0-3)            │
 *   │ rotation     │ 1 B    │ Rotation (0-3)               │
 *   │ temporary    │ 1 B    │ Temporary flag (bool)        │
 *   │ spawn_time   │ 8 B    │ Spawn timestamp (u64)        │
 *   └──────────────┴────────┴──────────────────────────────┘
 *   TOTAL: 25 bytes
 * 
 *   ObjectSystem Structure (variable size):
 *   ┌──────────────────┬────────┬──────────────────────────┐
 *   │ Field            │ Size   │ Purpose                  │
 *   ├──────────────────┼────────┼──────────────────────────┤
 *   │ definitions      │ 8 B    │ Pointer to definitions   │
 *   │ definition_count │ 4 B    │ Number of definitions    │
 *   │ objects          │ 8 B    │ Pointer to instances     │
 *   │ object_capacity  │ 4 B    │ Max object instances     │
 *   │ object_count     │ 4 B    │ Current object count     │
 *   │ initialized      │ 1 B    │ System ready flag        │
 *   └──────────────────┴────────┴──────────────────────────┘
 *   TOTAL: 29 bytes (+ dynamic arrays)
 * 
 *   Total memory for 30,000 definitions + 100,000 instances:
 *     Definitions: 30,000 * 350 B = 10.5 MB
 *     Instances:  100,000 * 25 B  = 2.5 MB
 *     TOTAL: 13 MB (reasonable for modern servers)
 * 
 * CACHE FILE FORMAT:
 * 
 * Object definitions are loaded from cache files at startup:
 * 
 *   Cache structure (simplified):
 *   ┌─────────────────────────────────────────────────────┐
 *   │ Object Definition Entry (variable length)          │
 *   ├──────────┬──────────────────────────────────────────┤
 *   │ ID       │ u16 (2 bytes)                            │
 *   │ Name     │ String (null-terminated)                 │
 *   │ Examine  │ String (null-terminated)                 │
 *   │ Type     │ u8 (1 byte)                              │
 *   │ Width    │ u8 (1 byte)                              │
 *   │ Length   │ u8 (1 byte)                              │
 *   │ Flags    │ u8 (solid, impenetrable, etc.)           │
 *   │ Models   │ Array of model IDs (variable)            │
 *   │ Actions  │ Array of strings (variable)              │
 *   └──────────┴──────────────────────────────────────────┘
 * 
 * EXAMPLE OBJECT DEFINITIONS:
 * 
 *   DOOR (ID 1519):
 *     name = "Door"
 *     examine = "A wooden door."
 *     type = OBJECT_TYPE_WALL (0)
 *     width = 1, length = 1
 *     solid = true (blocks movement)
 *     interactive = true (can be clicked)
 *     clipped = true (blocks camera)
 *     actions[0] = "Open"
 * 
 *   TREE (ID 1276):
 *     name = "Tree"
 *     examine = "A healthy tree."
 *     type = OBJECT_TYPE_INTERACTABLE (2)
 *     width = 1, length = 1
 *     solid = true (blocks movement)
 *     interactive = true (can be clicked)
 *     actions[0] = "Chop down"
 * 
 * USAGE PATTERN:
 * 
 *   1. Create and initialize system:
 *      ObjectSystem* objects = object_system_create(100000);
 *      object_system_init(objects);
 * 
 *   2. Spawn object instance:
 *      GameObject* door = object_spawn(objects, 1519, 3232, 3232, 0, 
 *                                      OBJECT_TYPE_WALL, 1);
 * 
 *   3. Find object at position:
 *      GameObject* obj = object_get_at(objects, 3232, 3232, 0, 
 *                                      OBJECT_TYPE_WALL);
 * 
 *   4. Get object definition:
 *      ObjectDefinition* def = object_get_definition(objects, obj->id);
 *      printf("Object name: %s\n", def->name);
 * 
 *   5. Despawn object:
 *      object_despawn(objects, obj);
 * 
 *   6. Cleanup:
 *      object_system_destroy(objects);
 * 
 ******************************************************************************/

#ifndef OBJECT_H
#define OBJECT_H

#include "types.h"
#include "position.h"

/*******************************************************************************
 * OBJECT TYPE ENUMERATION
 *******************************************************************************
 * 
 * Categorizes objects by their spatial behavior and collision properties.
 * 
 * These types match the RuneScape protocol exactly and determine:
 *   - How the object occupies space
 *   - How collision detection works
 *   - How the client renders the object
 *   - What tiles are blocked by the object
 ******************************************************************************/

/*
 * ObjectType - Spatial classification of game objects
 * 
 * VALUES:
 *   OBJECT_TYPE_WALL            = 0  (doors, fences, barriers)
 *   OBJECT_TYPE_WALL_DECORATION = 1  (torches, paintings, signs)
 *   OBJECT_TYPE_INTERACTABLE    = 2  (furniture, trees, rocks)
 *   OBJECT_TYPE_GROUND_DECORATION = 3 (flowers, stones, grass)
 * 
 * TYPE 0 - WALL:
 *   - Occupies EDGE between two tiles, not the tile itself
 *   - Rotation determines which edge (north/east/south/west)
 *   - Typically blocks movement in one direction
 *   - Examples: door, fence, wall segment, gate
 * 
 *   Visual example (door with rotation=1, facing NORTH):
 *   ┌──────────────┬──────────────┐
 *   │   Tile A     ═══DOOR═══     │  ═ = wall occupies north edge of Tile A
 *   │  (3232, 3232)│  Tile B      │      (blocks movement from A to B)
 *   └──────────────┴──────────────┘
 * 
 * TYPE 1 - WALL_DECORATION:
 *   - Attached to wall edge (same position rules as Type 0)
 *   - Does NOT block movement (purely cosmetic)
 *   - Examples: torch, painting, wall sign, banner
 * 
 *   Visual example (torch on north wall):
 *   ┌──────────────┬──────────────┐
 *   │   Tile A     *  Tile B      │  * = torch (no collision, just visual)
 *   │              │              │
 *   └──────────────┴──────────────┘
 * 
 * TYPE 2 - INTERACTABLE:
 *   - Occupies one or more FULL TILES
 *   - Has width and length (can be multi-tile)
 *   - Usually solid (blocks movement)
 *   - Usually interactive (can be clicked for actions)
 *   - Examples: tree, rock, chair, table, altar, furnace
 * 
 *   Visual example (2x2 tree):
 *   ┌──────────────┬──────────────┐
 *   │   TREE       │   TREE       │  Tree occupies 4 tiles
 *   │  (3232,3232) │  (3233,3232) │  All 4 tiles blocked
 *   ├──────────────┼──────────────┤
 *   │   TREE       │   TREE       │
 *   │  (3232,3233) │  (3233,3233) │
 *   └──────────────┴──────────────┘
 * 
 * TYPE 3 - GROUND_DECORATION:
 *   - Occupies tile visually but does NOT block movement
 *   - Cannot be interacted with (purely decorative)
 *   - Examples: flowers, small rocks, grass patches, mushrooms
 * 
 *   Visual example (flowers):
 *   ┌──────────────┬──────────────┐
 *   │    .*.*      │              │  .* = flowers (can walk over)
 *   │   FLOWERS    │              │
 *   └──────────────┴──────────────┘
 * 
 * PROTOCOL ENCODING:
 *   These values are sent to client in region update packets
 *   Client uses type to determine rendering and collision
 * 
 * COLLISION MATRIX:
 *   Type │ Blocks Movement │ Blocks Projectiles │ Interactive
 *   ─────┼─────────────────┼────────────────────┼────────────
 *     0  │  Usually YES    │   Depends (door)   │  Sometimes
 *     1  │      NO         │        NO          │  Rarely
 *     2  │  Usually YES    │   Depends (tree)   │  Usually YES
 *     3  │      NO         │        NO          │  NO
 * 
 * MEMORY: 1 byte per object (stored as u8)
 */
typedef enum {
    OBJECT_TYPE_WALL = 0,            /* Edge object: doors, fences, barriers */
    OBJECT_TYPE_WALL_DECORATION = 1, /* Edge decoration: torches, paintings */
    OBJECT_TYPE_INTERACTABLE = 2,    /* Tile object: furniture, trees, rocks */
    OBJECT_TYPE_GROUND_DECORATION = 3 /* Ground decoration: flowers, grass */
} ObjectType;

/*******************************************************************************
 * OBJECT DEFINITION - Static Template Data
 *******************************************************************************
 * 
 * Stores immutable properties shared by all instances of an object type.
 * Loaded from cache at server startup and never modified.
 * 
 * FLYWEIGHT PATTERN:
 *   One definition is shared by thousands of instances, saving memory.
 *   Example: All 10,000 "Normal tree" objects share one definition.
 * 
 * CACHE LOADING:
 *   Definitions are parsed from binary cache files:
 *     ./cache/objects/object_definitions.dat
 *   
 *   Format (simplified):
 *     [id:u16][name_len:u8][name:chars][examine_len:u8][examine:chars]
 *     [type:u8][width:u8][length:u8][flags:u8][model_count:u8]
 *     [models:u16*count][action_count:u8][actions:strings*count]
 * 
 * MEMORY USAGE:
 *   Approximately 350 bytes per definition
 *   30,000 definitions = 10.5 MB total
 ******************************************************************************/

/*
 * ObjectDefinition - Static object template
 * 
 * FIELDS:
 *   id:           Unique identifier (0-65535)
 *   name:         Display name shown to player ("Door", "Tree")
 *   examine:      Text shown when examining object ("A wooden door.")
 *   type:         ObjectType enum (wall, decoration, interactable, etc.)
 *   width:        Width in tiles along X axis (typically 1-5)
 *   length:       Length in tiles along Z axis (typically 1-5)
 *   solid:        Blocks player movement if true
 *   impenetrable: Blocks projectiles (arrows, spells) if true
 *   interactive:  Can be clicked for actions if true
 *   clipped:      Affects client-side collision and camera if true
 *   model_ids:    3D model IDs for rendering (up to 10 models)
 *   model_types:  Model type flags for each model
 *   model_count:  Number of valid models in model_ids array
 *   actions:      Right-click menu actions (up to 5, empty string = unused)
 * 
 * ACTION ARRAY:
 *   actions[0] = Primary action (left-click, e.g., "Open")
 *   actions[1] = Secondary action (right-click, e.g., "Knock")
 *   actions[2-4] = Additional actions (conditional, e.g., "Pick-lock")
 * 
 * SIZE CALCULATION:
 *   Large objects occupy width * length tiles
 *   
 *   Example: 3x2 table (width=3, length=2)
 *     Occupies 6 tiles:
 *       (x, z), (x+1, z), (x+2, z)
 *       (x, z+1), (x+1, z+1), (x+2, z+1)
 * 
 * COLLISION FLAGS:
 *   solid=true, impenetrable=false: Blocks walking, not projectiles (bush)
 *   solid=true, impenetrable=true:  Blocks both (wall, tree)
 *   solid=false, impenetrable=false: Blocks neither (flowers)
 * 
 * MODEL SYSTEM:
 *   Multiple models can be combined for complex objects
 *   Example: Chair has 4 models (seat, backrest, 2 legs)
 *   Client renders all models at same position with different transforms
 * 
 * EXAMPLE DEFINITION (Door ID 1519):
 *   {
 *     id = 1519,
 *     name = "Door",
 *     examine = "A wooden door.",
 *     type = OBJECT_TYPE_WALL,
 *     width = 1,
 *     length = 1,
 *     solid = true,
 *     impenetrable = false,
 *     interactive = true,
 *     clipped = true,
 *     model_ids = {2345, 0, 0, ...},
 *     model_types = {0, 0, 0, ...},
 *     model_count = 1,
 *     actions = {"Open", "", "", "", "Examine"}
 *   }
 * 
 * COMPLEXITY: O(1) lookup by ID (array indexed by object ID)
 */
typedef struct {
    u16 id;                 /* Unique object identifier (0-65535) */
    char name[64];          /* Display name, e.g., "Door", "Tree" */
    char examine[128];      /* Examine text, e.g., "A wooden door." */
    u8 type;                /* ObjectType: wall, decoration, interactable, etc. */
    u8 width;               /* Width in tiles (X axis, typically 1-5) */
    u8 length;              /* Length in tiles (Z axis, typically 1-5) */
    bool solid;             /* Blocks player movement if true */
    bool impenetrable;      /* Blocks projectiles if true */
    bool interactive;       /* Can be clicked for actions if true */
    bool clipped;           /* Affects client collision/camera if true */
    u16 model_ids[10];      /* 3D model IDs for rendering (up to 10) */
    u16 model_types[10];    /* Model type/variant for each model */
    u8 model_count;         /* Number of models in model_ids (0-10) */
    char actions[5][32];    /* Right-click actions (5 slots, 32 chars each) */
} ObjectDefinition;

/*******************************************************************************
 * GAME OBJECT - Dynamic Instance State
 *******************************************************************************
 * 
 * Represents a single object instance in the game world.
 * Stores mutable per-object state (position, rotation, spawn time).
 * 
 * INSTANCE vs DEFINITION:
 *   GameObject stores WHERE and WHEN (position, time)
 *   ObjectDefinition stores WHAT and HOW (name, properties)
 * 
 * MEMORY EFFICIENCY:
 *   Only 25 bytes per instance (compared to 350 bytes if we duplicated
 *   the definition data for each instance)
 ******************************************************************************/

/*
 * GameObject - Object instance in world
 * 
 * FIELDS:
 *   id:         Object definition ID (links to ObjectDefinition)
 *   position:   3D coordinates (x, z, height)
 *   type:       ObjectType (duplicated from definition for fast filtering)
 *   rotation:   Cardinal direction (0=WEST, 1=NORTH, 2=EAST, 3=SOUTH)
 *   temporary:  If true, object was spawned at runtime (not from map cache)
 *   spawn_time: Timestamp when object was spawned (for temporary objects)
 * 
 * ID FIELD:
 *   When id=0, this slot is FREE (not occupied by any object)
 *   Used for efficient slot allocation in object array
 * 
 * ROTATION SYSTEM:
 *   Rotation affects both visual orientation and collision geometry
 *   
 *   Rotation 0 (WEST, 0 degrees):    Rotation 1 (NORTH, 90 degrees):
 *     Object faces west                 Object faces north
 *     ←                                 ↑
 *   
 *   Rotation 2 (EAST, 180 degrees):  Rotation 3 (SOUTH, 270 degrees):
 *     Object faces east                 Object faces south
 *     →                                 ↓
 * 
 *   For rectangular objects (width != length), rotation swaps dimensions:
 *     2x1 table with rotation 0: occupies (x, z) and (x+1, z)
 *     2x1 table with rotation 1: occupies (x, z) and (x, z+1)
 * 
 * TEMPORARY OBJECTS:
 *   temporary=true: Object was spawned dynamically (tree stump, fire, etc.)
 *   temporary=false: Object is permanent (loaded from map cache)
 * 
 *   Temporary object lifecycle:
 *     1. Player chops tree (permanent object)
 *     2. Server despawns tree
 *     3. Server spawns stump (temporary=true, spawn_time=current_tick)
 *     4. After timeout (60 seconds), server despawns stump
 *     5. Server respawns tree (permanent object)
 * 
 * SPAWN_TIME USAGE:
 *   For temporary objects, spawn_time stores tick count when spawned
 *   Cleanup system checks: if (current_tick - spawn_time > timeout) despawn
 *   
 *   Example:
 *     spawn_time = 10000 (tick when stump spawned)
 *     current_tick = 10100 (current game tick)
 *     timeout = 100 ticks (60 seconds at 600ms per tick)
 *     if (10100 - 10000 > 100) -> despawn stump
 * 
 * FREE SLOT DETECTION:
 *   To find free slot in object array:
 *     for (i = 0; i < capacity; i++)
 *       if (objects[i].id == 0) return &objects[i];  // Found free slot
 * 
 * EXAMPLE INSTANCE (Door at Lumbridge):
 *   {
 *     id = 1519,              // Links to "Door" definition
 *     position = {3232, 3232, 0},
 *     type = OBJECT_TYPE_WALL,
 *     rotation = 1,           // Facing north
 *     temporary = false,      // Permanent map object
 *     spawn_time = 0          // Not applicable for permanent objects
 *   }
 * 
 * COMPLEXITY: O(1) storage (25 bytes, constant size)
 */
typedef struct {
    u16 id;             /* Object definition ID (0 = free slot) */
    Position position;  /* 3D world coordinates (x, z, height) */
    u8 type;            /* ObjectType (wall, decoration, interactable, etc.) */
    u8 rotation;        /* Cardinal direction: 0=W, 1=N, 2=E, 3=S */
    bool temporary;     /* True if spawned at runtime (not from map cache) */
    u64 spawn_time;     /* Tick count when spawned (for temporary objects) */
} GameObject;

/*******************************************************************************
 * OBJECT SYSTEM - Global Object Manager
 *******************************************************************************
 * 
 * Manages all object definitions and instances in the game world.
 * Provides allocation, lookup, and lifecycle management.
 * 
 * ARCHITECTURE:
 *   One global ObjectSystem instance manages:
 *     1. Array of all object definitions (loaded from cache)
 *     2. Array of all object instances (spawned in world)
 *     3. Free slot management for instance allocation
 * 
 * MEMORY MANAGEMENT:
 *   Uses dynamic arrays (allocated on heap) for flexibility
 *   Capacity specified at creation time
 *   Uses free slot reuse (id=0 marks free slots)
 * 
 * INITIALIZATION SEQUENCE:
 *   1. object_system_create()  - Allocate arrays
 *   2. object_system_init()    - Load definitions from cache
 *   3. object_spawn()          - Spawn object instances
 *   4. object_system_destroy() - Free all memory
 ******************************************************************************/

/*
 * ObjectSystem - Global object management state
 * 
 * FIELDS:
 *   definitions:      Array of object templates (from cache)
 *   definition_count: Number of definitions loaded
 *   objects:          Array of object instances (in world)
 *   object_capacity:  Maximum object instances (array size)
 *   object_count:     Current number of spawned objects
 *   initialized:      True if system is ready for use
 * 
 * DEFINITION STORAGE:
 *   definitions is indexed directly by object ID:
 *     ObjectDefinition* def = &definitions[1519];  // O(1) lookup
 *   
 *   Sparse array (many unused IDs), but fast lookup is worth the waste
 *   
 *   Example capacity:
 *     definition_count = 30000 (30K definitions)
 *     Memory: 30000 * 350 bytes = 10.5 MB
 * 
 * INSTANCE STORAGE:
 *   objects is a densely-packed array with free slot reuse:
 *     - Active objects have id != 0
 *     - Free slots have id == 0
 *   
 *   Free slot allocation:
 *     Linear search for first id==0 slot: O(capacity) worst case
 *     Could be optimized with free list: O(1) allocation
 *   
 *   Example capacity:
 *     object_capacity = 100000 (100K max instances)
 *     Memory: 100000 * 25 bytes = 2.5 MB
 * 
 * OBJECT_COUNT TRACKING:
 *   Incremented on spawn, decremented on despawn
 *   Used to check if capacity is full before allocation
 *   
 *   Invariant: object_count <= object_capacity
 * 
 * INITIALIZED FLAG:
 *   Prevents usage before definitions are loaded
 *   Functions check initialized flag before operating
 *   
 *   Lifecycle:
 *     created:     initialized = false
 *     init called: initialized = true
 *     destroyed:   (memory freed, no access possible)
 * 
 * GLOBAL INSTANCE:
 *   extern ObjectSystem* g_objects;
 *   
 *   Single global instance for the entire server
 *   All subsystems access objects through g_objects
 *   
 *   Example usage:
 *     g_objects = object_system_create(100000);
 *     object_system_init(g_objects);
 *     GameObject* door = object_spawn(g_objects, 1519, ...);
 * 
 * THREAD SAFETY:
 *   Current implementation is NOT thread-safe
 *   All access must be from game thread
 *   For multi-threaded servers, would need mutex or per-region locks
 * 
 * COMPLEXITY:
 *   Storage: O(definition_count + object_capacity) space
 *   Definition lookup: O(1) time (direct array indexing)
 *   Instance spawn: O(capacity) worst case (linear free slot search)
 *   Instance lookup: O(capacity) worst case (linear search by position)
 */
typedef struct {
    ObjectDefinition* definitions;  /* Array of object templates (from cache) */
    u32 definition_count;           /* Number of definitions loaded */
    GameObject* objects;            /* Array of object instances (in world) */
    u32 object_capacity;            /* Maximum object instances */
    u32 object_count;               /* Current number of spawned objects */
    bool initialized;               /* True if system is ready */
} ObjectSystem;

/*******************************************************************************
 * GLOBAL OBJECT SYSTEM INSTANCE
 *******************************************************************************
 * 
 * Single global instance for entire server.
 * Initialized at server startup, destroyed at shutdown.
 * 
 * USAGE:
 *   extern ObjectSystem* g_objects;  // Declare in header
 *   ObjectSystem* g_objects = NULL;  // Define in .c file
 *   
 *   In main():
 *     g_objects = object_system_create(100000);
 *     object_system_init(g_objects);
 *     ...
 *     object_system_destroy(g_objects);
 * 
 * GLOBAL vs PARAMETER:
 *   Functions take ObjectSystem* parameter for testability
 *   In production, callers pass g_objects
 *   In tests, callers can create isolated ObjectSystem instances
 ******************************************************************************/
extern ObjectSystem* g_objects;

/*******************************************************************************
 * OBJECT SYSTEM LIFECYCLE FUNCTIONS
 ******************************************************************************/

/*
 * object_system_create - Allocate object system with capacity
 * 
 * @param capacity  Maximum number of object instances
 * @return          Pointer to new ObjectSystem, or NULL on allocation failure
 * 
 * ALGORITHM:
 *   1. Allocate ObjectSystem struct on heap
 *   2. Allocate objects array with specified capacity
 *   3. Zero-initialize all memory
 *   4. Set initialized=false (must call object_system_init later)
 * 
 * MEMORY ALLOCATION:
 *   Heap allocation (malloc/calloc):
 *     - ObjectSystem struct: ~29 bytes
 *     - GameObject array: capacity * 25 bytes
 *     - Total: ~(capacity * 25) bytes
 *   
 *   Example for capacity=100000:
 *     100000 * 25 bytes = 2.5 MB
 * 
 * ZERO-INITIALIZATION:
 *   calloc() zeros memory, so:
 *     - All object slots have id=0 (marked as free)
 *     - object_count starts at 0
 *     - initialized starts at false
 * 
 * FAILURE HANDLING:
 *   Returns NULL if allocation fails
 *   Caller must check return value
 *   
 *   Example:
 *     ObjectSystem* sys = object_system_create(100000);
 *     if (!sys) {
 *       fprintf(stderr, "Out of memory!\n");
 *       exit(1);
 *     }
 * 
 * CLEANUP ON PARTIAL FAILURE:
 *   If objects array allocation fails:
 *     - Free ObjectSystem struct before returning NULL
 *     - Prevents memory leak from partial allocation
 * 
 * EXAMPLE USAGE:
 *   ObjectSystem* objects = object_system_create(100000);
 *   if (!objects) {
 *     fprintf(stderr, "Failed to create object system\n");
 *     return -1;
 *   }
 * 
 * COMPLEXITY: O(capacity) time and space (for zero-initialization)
 */
ObjectSystem* object_system_create(u32 capacity);

/*
 * object_system_destroy - Free all memory used by object system
 * 
 * @param objects  Object system to destroy (can be NULL)
 * 
 * ALGORITHM:
 *   1. Check if objects is NULL (safe to call on NULL pointer)
 *   2. Free definitions array (if allocated)
 *   3. Free objects array
 *   4. Free ObjectSystem struct
 * 
 * MEMORY DEALLOCATION:
 *   Frees heap allocations in reverse order of creation:
 *     1. definitions array (from object_system_init)
 *     2. objects array (from object_system_create)
 *     3. ObjectSystem struct (from object_system_create)
 * 
 * NULL SAFETY:
 *   Safe to call with NULL pointer (no-op)
 *   Safe to call multiple times (though second call would crash)
 *   
 *   Example:
 *     ObjectSystem* sys = NULL;
 *     object_system_destroy(sys);  // Safe, does nothing
 * 
 * CLEANUP RESPONSIBILITY:
 *   This function does NOT:
 *     - Send despawn packets to clients
 *     - Call cleanup handlers for objects
 *     - Save state to database
 *   
 *   Caller must handle game logic cleanup before destroying system
 * 
 * EXAMPLE USAGE:
 *   void shutdown_server() {
 *     // Save world state to database
 *     save_objects_to_db(g_objects);
 *     
 *     // Free memory
 *     object_system_destroy(g_objects);
 *     g_objects = NULL;
 *   }
 * 
 * COMPLEXITY: O(1) time (just freeing pointers)
 */
void object_system_destroy(ObjectSystem* objects);

/*
 * object_system_init - Load object definitions from cache
 * 
 * @param objects  Object system to initialize
 * @return         true on success, false on failure
 * 
 * ALGORITHM:
 *   1. Check if already initialized (return false if so)
 *   2. Allocate definitions array (30,000 entries)
 *   3. Load definitions from cache file
 *   4. Parse each definition entry
 *   5. Set initialized=true
 * 
 * CURRENT IMPLEMENTATION:
 *   TODO: Full cache loading not yet implemented
 *   Currently hardcodes a few example definitions:
 *     - Door (ID 1519)
 *     - Tree (ID 1276)
 * 
 * CACHE FILE FORMAT (simplified):
 *   Binary file: ./cache/objects/object_definitions.dat
 *   
 *   Structure:
 *     [definition_count:u32]
 *     For each definition:
 *       [id:u16]
 *       [name_length:u8][name:chars]
 *       [examine_length:u8][examine:chars]
 *       [type:u8][width:u8][length:u8]
 *       [flags:u8]  (packed: solid|impenetrable|interactive|clipped)
 *       [model_count:u8]
 *       For each model:
 *         [model_id:u16][model_type:u16]
 *       [action_count:u8]
 *       For each action:
 *         [action_length:u8][action:chars]
 * 
 * MEMORY ALLOCATION:
 *   Allocates definitions array:
 *     30,000 definitions * 350 bytes = 10.5 MB
 * 
 * ERROR HANDLING:
 *   Returns false if:
 *     - objects is NULL
 *     - Already initialized (initialized=true)
 *     - Definitions allocation fails
 *     - Cache file cannot be opened/parsed
 * 
 * EXAMPLE HARDCODED DEFINITION (Door):
 *   definitions[1519].id = 1519;
 *   strcpy(definitions[1519].name, "Door");
 *   strcpy(definitions[1519].examine, "A wooden door.");
 *   definitions[1519].type = OBJECT_TYPE_WALL;
 *   definitions[1519].width = 1;
 *   definitions[1519].length = 1;
 *   definitions[1519].solid = true;
 *   definitions[1519].interactive = true;
 *   definitions[1519].clipped = true;
 *   strcpy(definitions[1519].actions[0], "Open");
 * 
 * INITIALIZATION SEQUENCE:
 *   1. Call object_system_create() first
 *   2. Call object_system_init() second
 *   3. System is now ready for object_spawn() calls
 * 
 * IDEMPOTENCY:
 *   NOT idempotent - calling twice returns false
 *   initialized flag prevents re-initialization
 * 
 * EXAMPLE USAGE:
 *   ObjectSystem* objects = object_system_create(100000);
 *   if (!object_system_init(objects)) {
 *     fprintf(stderr, "Failed to load object definitions\n");
 *     object_system_destroy(objects);
 *     return -1;
 *   }
 *   printf("Loaded %u object definitions\n", objects->definition_count);
 * 
 * COMPLEXITY: O(definition_count) time for cache loading
 */
bool object_system_init(ObjectSystem* objects);

/*
 * object_get_definition - Lookup object definition by ID
 * 
 * @param objects  Object system (must be initialized)
 * @param id       Object definition ID (0-65535)
 * @return         Pointer to definition, or NULL if not found
 * 
 * ALGORITHM:
 *   1. Validate parameters (objects != NULL, initialized == true)
 *   2. Check if id is within bounds (id < definition_count)
 *   3. Return pointer to definitions[id]
 * 
 * DIRECT INDEXING:
 *   Uses id as array index for O(1) lookup
 *   No searching required (trade memory for speed)
 * 
 * BOUNDS CHECKING:
 *   Returns NULL if id >= definition_count
 *   Prevents out-of-bounds access
 * 
 * RETURN VALUE:
 *   Returns pointer to definition (NOT a copy)
 *   Caller must NOT modify returned definition
 *   Definition remains valid until object_system_destroy()
 * 
 * EXAMPLE USAGE:
 *   ObjectDefinition* door_def = object_get_definition(g_objects, 1519);
 *   if (!door_def) {
 *     fprintf(stderr, "Unknown object ID: 1519\n");
 *     return;
 *   }
 *   printf("Object name: %s\n", door_def->name);
 *   printf("Examine: %s\n", door_def->examine);
 *   printf("Primary action: %s\n", door_def->actions[0]);
 * 
 * USE CASE - PLAYER INTERACTION:
 *   Player clicks object at (3232, 3232):
 *     1. Find GameObject at that position
 *     2. Get definition: def = object_get_definition(sys, obj->id)
 *     3. Check if interactive: if (!def->interactive) return;
 *     4. Execute action: handle_object_action(def->actions[0])
 * 
 * COMPLEXITY: O(1) time (direct array indexing)
 */
ObjectDefinition* object_get_definition(ObjectSystem* objects, u16 id);

/*******************************************************************************
 * OBJECT INSTANCE FUNCTIONS
 ******************************************************************************/

/*
 * object_spawn - Create new object instance in world
 * 
 * @param objects    Object system (must be initialized)
 * @param object_id  Object definition ID (must exist in definitions)
 * @param x          World X coordinate
 * @param z          World Z coordinate
 * @param height     Height level (0-3)
 * @param type       Object type (wall, decoration, interactable, etc.)
 * @param rotation   Cardinal direction (0=W, 1=N, 2=E, 3=S)
 * @return           Pointer to new GameObject, or NULL on failure
 * 
 * ALGORITHM:
 *   1. Validate parameters (objects initialized, capacity not exceeded)
 *   2. Find free slot in objects array (id == 0)
 *   3. Initialize GameObject with provided parameters
 *   4. Increment object_count
 *   5. Return pointer to new object
 * 
 * FREE SLOT ALLOCATION:
 *   Linear search for first id==0 slot:
 *     for (i = 0; i < capacity; i++)
 *       if (objects[i].id == 0) return &objects[i];
 *   
 *   O(capacity) worst case (all slots full except last one)
 *   Could be optimized with free list for O(1) allocation
 * 
 * CAPACITY CHECK:
 *   Returns NULL if object_count >= object_capacity
 *   Prevents array overflow
 * 
 * POSITION INITIALIZATION:
 *   Uses position_init() to set 3D coordinates:
 *     position_init(&obj->position, x, z, height);
 * 
 * TEMPORARY FLAG:
 *   Always sets temporary=false (permanent object)
 *   For temporary objects, caller should manually set flag after spawning
 * 
 * SPAWN_TIME:
 *   Initialized to 0 (not applicable for permanent objects)
 *   For temporary objects, caller should set to current tick
 * 
 * EXAMPLE USAGE (permanent object):
 *   GameObject* door = object_spawn(g_objects, 1519, 3232, 3232, 0,
 *                                   OBJECT_TYPE_WALL, 1);
 *   if (!door) {
 *     fprintf(stderr, "Failed to spawn door (capacity full?)\n");
 *     return;
 *   }
 *   printf("Spawned door at (%u, %u)\n", door->position.x, door->position.z);
 * 
 * EXAMPLE USAGE (temporary object):
 *   GameObject* stump = object_spawn(g_objects, 1342, 3232, 3232, 0,
 *                                    OBJECT_TYPE_INTERACTABLE, 0);
 *   if (stump) {
 *     stump->temporary = true;
 *     stump->spawn_time = get_current_tick();
 *   }
 * 
 * NETWORK SYNCHRONIZATION:
 *   After spawning, caller should:
 *     1. Add object to region spatial index
 *     2. Send spawn packet to nearby players
 *   
 *   Example:
 *     GameObject* obj = object_spawn(...);
 *     region_add_object(region, obj);
 *     send_object_spawn_packet(players_nearby, obj);
 * 
 * COLLISION UPDATES:
 *   After spawning, caller should update collision map:
 *     if (def->solid) {
 *       collision_add_object(collision_map, obj);
 *     }
 * 
 * COMPLEXITY: O(capacity) worst case for free slot search
 */
GameObject* object_spawn(ObjectSystem* objects, u16 object_id, u32 x, u32 z, 
                         u32 height, u8 type, u8 rotation);

/*
 * object_despawn - Remove object instance from world
 * 
 * @param objects  Object system
 * @param object   Object to despawn (must be valid)
 * 
 * ALGORITHM:
 *   1. Validate parameters (objects != NULL, object != NULL, object->id != 0)
 *   2. Set object->id = 0 (mark slot as free)
 *   3. Decrement object_count
 * 
 * FREE SLOT MARKING:
 *   Setting id=0 makes slot available for reuse
 *   Next object_spawn() can allocate this slot
 * 
 * OBJECT COUNT TRACKING:
 *   Decrements object_count to maintain accurate count
 *   
 *   Invariant after despawn:
 *     object_count == number of objects with id != 0
 * 
 * PARTIAL CLEANUP:
 *   Only marks slot as free, does NOT:
 *     - Zero other fields (optimization: unnecessary)
 *     - Free memory (objects array is pre-allocated)
 *     - Send network packets (caller's responsibility)
 * 
 * EXAMPLE USAGE:
 *   GameObject* tree = object_get_at(g_objects, 3232, 3232, 0,
 *                                    OBJECT_TYPE_INTERACTABLE);
 *   if (tree) {
 *     object_despawn(g_objects, tree);
 *     printf("Despawned tree at (%u, %u)\n", 
 *            tree->position.x, tree->position.z);
 *   }
 * 
 * TEMPORARY OBJECT CLEANUP:
 *   Periodic tick checks for expired temporary objects:
 *     for (i = 0; i < capacity; i++) {
 *       GameObject* obj = &objects[i];
 *       if (obj->id != 0 && obj->temporary) {
 *         if (current_tick - obj->spawn_time > timeout) {
 *           object_despawn(objects, obj);
 *         }
 *       }
 *     }
 * 
 * NETWORK SYNCHRONIZATION:
 *   After despawning, caller should:
 *     1. Remove object from region spatial index
 *     2. Send despawn packet to nearby players
 *   
 *   Example:
 *     object_despawn(g_objects, obj);
 *     region_remove_object(region, obj);
 *     send_object_despawn_packet(players_nearby, obj);
 * 
 * COLLISION UPDATES:
 *   After despawning, caller should update collision map:
 *     ObjectDefinition* def = object_get_definition(g_objects, obj->id);
 *     if (def->solid) {
 *       collision_remove_object(collision_map, obj);
 *     }
 * 
 * DANGLING POINTER WARNING:
 *   After despawn, caller must NOT use object pointer
 *   Slot may be reused by next object_spawn()
 *   
 *   BAD:
 *     object_despawn(sys, obj);
 *     printf("Object ID: %u\n", obj->id);  // WRONG! obj->id is now 0
 *   
 *   GOOD:
 *     u16 old_id = obj->id;
 *     object_despawn(sys, obj);
 *     printf("Despawned object ID: %u\n", old_id);
 * 
 * COMPLEXITY: O(1) time
 */
void object_despawn(ObjectSystem* objects, GameObject* object);

/*
 * object_get_at - Find object at specific position and type
 * 
 * @param objects  Object system (must be initialized)
 * @param x        World X coordinate
 * @param z        World Z coordinate
 * @param height   Height level (0-3)
 * @param type     Object type to search for
 * @return         Pointer to matching GameObject, or NULL if not found
 * 
 * ALGORITHM:
 *   1. Validate parameters (objects initialized)
 *   2. Linear search through objects array
 *   3. For each object:
 *        - Check if id != 0 (slot is occupied)
 *        - Check if position matches (x, z, height)
 *        - Check if type matches
 *   4. Return first match, or NULL if none found
 * 
 * LINEAR SEARCH:
 *   O(capacity) worst case (must check all slots)
 *   Could be optimized with spatial indexing:
 *     - Hash map by position: O(1) lookup
 *     - Region grid: O(objects_in_region) lookup
 * 
 * TYPE FILTERING:
 *   Multiple objects can exist at same position with different types
 *   
 *   Example at (3232, 3232):
 *     - Type 0 (WALL): Door on north edge
 *     - Type 2 (INTERACTABLE): Chair on tile
 *     - Type 3 (GROUND_DECORATION): Flowers on tile
 *   
 *   Caller specifies which type to find
 * 
 * POSITION MATCHING:
 *   Exact match required for all three coordinates:
 *     obj->position.x == x
 *     obj->position.z == z
 *     obj->position.height == height
 * 
 * FIRST MATCH:
 *   Returns first matching object found
 *   If multiple objects match (should not happen), only returns first
 * 
 * EXAMPLE USAGE (find door):
 *   GameObject* door = object_get_at(g_objects, 3232, 3232, 0,
 *                                    OBJECT_TYPE_WALL);
 *   if (door) {
 *     ObjectDefinition* def = object_get_definition(g_objects, door->id);
 *     printf("Found %s at (%u, %u)\n", def->name,
 *            door->position.x, door->position.z);
 *   } else {
 *     printf("No wall object at (3232, 3232)\n");
 *   }
 * 
 * EXAMPLE USAGE (check for interactable):
 *   GameObject* obj = object_get_at(g_objects, x, z, height,
 *                                   OBJECT_TYPE_INTERACTABLE);
 *   if (obj) {
 *     // Tile has interactable object, block movement
 *     return false;
 *   }
 *   // Tile is clear, allow movement
 *   return true;
 * 
 * USE CASE - PLAYER CLICK:
 *   Player clicks tile at (3232, 3232):
 *     1. Try to find wall object:
 *          obj = object_get_at(sys, 3232, 3232, 0, OBJECT_TYPE_WALL);
 *     2. If found, handle wall interaction (open door, etc.)
 *     3. Otherwise, try interactable:
 *          obj = object_get_at(sys, 3232, 3232, 0, OBJECT_TYPE_INTERACTABLE);
 *     4. If found, handle interactable (chop tree, etc.)
 * 
 * OPTIMIZATION OPPORTUNITY:
 *   For production servers with many objects, consider:
 *     - Hash map: position -> list of objects
 *     - Region grid: divide map into chunks, index objects by chunk
 *     - Quadtree: hierarchical spatial partitioning
 *   
 *   With region grid (64x64 tiles per region):
 *     - Index by region ID: O(1) to get region
 *     - Search within region: O(objects_in_region)
 *     - Typical speedup: 100-1000x faster
 * 
 * COMPLEXITY: O(capacity) worst case (linear search all objects)
 */
GameObject* object_get_at(ObjectSystem* objects, u32 x, u32 z, u32 height, u8 type);

#endif /* OBJECT_H */
