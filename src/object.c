/*******************************************************************************
 * OBJECT.C - Game Object System Implementation
 *******************************************************************************
 * 
 * This file implements the game object management system for a RuneScape
 * private server. It provides allocation, lifecycle, and lookup operations
 * for both object definitions (static templates) and object instances
 * (dynamic world state).
 * 
 * KEY ALGORITHMS IMPLEMENTED:
 *   1. Free slot allocation (linear search, O(n) worst case)
 *   2. Definition lookup by ID (direct indexing, O(1))
 *   3. Instance lookup by position (linear search, O(n) worst case)
 *   4. Memory pool management with slot reuse
 * 
 * EDUCATIONAL CONCEPTS:
 *   - Flyweight pattern (definition/instance separation)
 *   - Memory pool allocation (pre-allocated arrays)
 *   - Free list management (slot reuse)
 *   - Spatial lookup (position-based queries)
 *   - Cache file parsing (binary format)
 * 
 * MEMORY MANAGEMENT STRATEGY:
 * 
 *   DEFINITION STORAGE (sparse array):
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │ definitions[0]     │ definitions[1]     │ ... │ defs[29999] │
 *   ├────────────────────┼────────────────────┼─────┼─────────────┤
 *   │ Empty (ID 0 unused)│ Door (ID 1 exists) │ ... │ Altar       │
 *   └─────────────────────────────────────────────────────────────┘
 *   
 *   Trade-off: Wastes space for unused IDs, but O(1) lookup
 *   Alternative: Hash map (saves space, slightly slower lookup)
 * 
 *   INSTANCE STORAGE (dense array with free slots):
 *   ┌────────────┬────────────┬────────────┬────────────┬────────┐
 *   │ objects[0] │ objects[1] │ objects[2] │ objects[3] │  ...   │
 *   ├────────────┼────────────┼────────────┼────────────┼────────┤
 *   │ id=1519    │ id=0 FREE  │ id=1276    │ id=0 FREE  │  ...   │
 *   │ Door at    │ Available  │ Tree at    │ Available  │        │
 *   │ (3232,3232)│ for spawn  │ (3240,3240)│ for spawn  │        │
 *   └────────────┴────────────┴────────────┴────────────┴────────┘
 *   
 *   Free slot detection: id==0 means slot is available
 *   Allocation: Linear search for first id==0 slot
 *   Deallocation: Set id=0 to mark slot as free
 * 
 * OBJECT LIFECYCLE FLOWCHART:
 * 
 *   SERVER STARTUP:
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │ 1. object_system_create(capacity)                           │
 *   │    - Allocate ObjectSystem struct                           │
 *   │    - Allocate objects array (capacity * sizeof(GameObject)) │
 *   │    - Zero-initialize all memory                             │
 *   │    - Set initialized=false                                  │
 *   └──────────────────┬──────────────────────────────────────────┘
 *                      │
 *   ┌──────────────────▼──────────────────────────────────────────┐
 *   │ 2. object_system_init(objects)                              │
 *   │    - Allocate definitions array (30,000 entries)            │
 *   │    - Open cache file: ./cache/objects/definitions.dat       │
 *   │    - Parse binary data for each definition                  │
 *   │    - Store in definitions[id] for O(1) lookup               │
 *   │    - Set initialized=true                                   │
 *   └──────────────────┬──────────────────────────────────────────┘
 *                      │
 *   ┌──────────────────▼──────────────────────────────────────────┐
 *   │ 3. Load map data (per region)                               │
 *   │    - Open region file: ./cache/maps/region_X_Y.dat          │
 *   │    - For each object in region:                             │
 *   │        object_spawn(sys, id, x, z, height, type, rotation)  │
 *   └──────────────────┬──────────────────────────────────────────┘
 *                      │
 *                      │
 *   RUNTIME:           │
 *   ┌──────────────────▼──────────────────────────────────────────┐
 *   │ 4. Player interaction (object click)                        │
 *   │    - Find object: obj = object_get_at(sys, x, z, h, type)   │
 *   │    - Get definition: def = object_get_definition(sys, id)   │
 *   │    - Execute action handler                                 │
 *   │    - Possibly despawn/respawn (doors, trees, etc.)          │
 *   └──────────────────┬──────────────────────────────────────────┘
 *                      │
 *   ┌──────────────────▼──────────────────────────────────────────┐
 *   │ 5. Temporary object management (every tick)                 │
 *   │    - Iterate all objects                                    │
 *   │    - For each temporary object:                             │
 *   │        if (current_tick - spawn_time > timeout)             │
 *   │          object_despawn(sys, obj)                           │
 *   └──────────────────┬──────────────────────────────────────────┘
 *                      │
 *   SERVER SHUTDOWN:   │
 *   ┌──────────────────▼──────────────────────────────────────────┐
 *   │ 6. object_system_destroy(objects)                           │
 *   │    - Free definitions array                                 │
 *   │    - Free objects array                                     │
 *   │    - Free ObjectSystem struct                               │
 *   └─────────────────────────────────────────────────────────────┘
 * 
 * ALLOCATION ALGORITHM ANALYSIS:
 * 
 *   CURRENT IMPLEMENTATION (linear free slot search):
 *     Time complexity: O(capacity) worst case
 *     Space complexity: O(1) extra (no auxiliary structures)
 *     
 *     Worst case scenario:
 *       - 99,999 objects spawned (capacity = 100,000)
 *       - All in first 99,999 slots
 *       - Last slot (99,999) is free
 *       - Next spawn requires checking all 99,999 slots
 *     
 *     Best case scenario:
 *       - First slot is free
 *       - O(1) allocation
 * 
 *   OPTIMIZED ALTERNATIVE (free list):
 *     Maintain linked list of free slots:
 *       - At creation: free_list = [0, 1, 2, ..., capacity-1]
 *       - At spawn: pop from free_list
 *       - At despawn: push to free_list
 *     
 *     Time complexity: O(1) for both spawn and despawn
 *     Space complexity: O(capacity) for free list
 *     
 *     Trade-off: Faster allocation, but uses more memory
 * 
 * LOOKUP ALGORITHM ANALYSIS:
 * 
 *   DEFINITION LOOKUP (by ID):
 *     Algorithm: Direct array indexing
 *     Time: O(1)
 *     Space: O(definition_count)
 *     
 *     Example:
 *       ObjectDefinition* def = &definitions[1519];
 *       No loop required, just pointer arithmetic!
 * 
 *   INSTANCE LOOKUP (by position):
 *     Algorithm: Linear search
 *     Time: O(capacity) worst case
 *     Space: O(1) extra
 *     
 *     Optimization: Spatial indexing
 *       - Divide world into regions (64x64 tiles each)
 *       - Hash map: region_id -> list of objects
 *       - Lookup: O(1) to find region, O(objects_in_region) to search
 *       - Typical speedup: 100-1000x faster
 * 
 * CACHE FILE FORMAT (simplified):
 * 
 *   Binary structure for object definitions:
 *   
 *   ┌──────────────────────────────────────────────────────────────┐
 *   │ File Header                                                  │
 *   ├──────────────────────────────────────────────────────────────┤
 *   │ [definition_count:u32]  Number of definitions in file        │
 *   └──────────────────────────────────────────────────────────────┘
 *   
 *   ┌──────────────────────────────────────────────────────────────┐
 *   │ Definition Entry (variable length, repeated for each object) │
 *   ├──────────────────────────────────────────────────────────────┤
 *   │ [id:u16]                Object definition ID (0-65535)       │
 *   │                                                              │
 *   │ [name_length:u8]        Length of name string                │
 *   │ [name:char*length]      Object name (e.g., "Door")           │
 *   │                                                              │
 *   │ [examine_length:u8]     Length of examine string             │
 *   │ [examine:char*length]   Examine text (e.g., "A wooden door") │
 *   │                                                              │
 *   │ [type:u8]               Object type (0-3)                    │
 *   │ [width:u8]              Width in tiles (1-10)                │
 *   │ [length:u8]             Length in tiles (1-10)               │
 *   │                                                              │
 *   │ [flags:u8]              Packed boolean flags:                │
 *   │                         bit 0: solid                         │
 *   │                         bit 1: impenetrable                  │
 *   │                         bit 2: interactive                   │
 *   │                         bit 3: clipped                       │
 *   │                         bits 4-7: unused (reserved)          │
 *   │                                                              │
 *   │ [model_count:u8]        Number of 3D models (0-10)           │
 *   │ For each model:                                              │
 *   │   [model_id:u16]        3D model ID for rendering            │
 *   │   [model_type:u16]      Model variant/type flags             │
 *   │                                                              │
 *   │ [action_count:u8]       Number of actions (0-5)              │
 *   │ For each action:                                             │
 *   │   [action_length:u8]    Length of action string              │
 *   │   [action:char*length]  Action text (e.g., "Open")           │
 *   └──────────────────────────────────────────────────────────────┘
 * 
 *   Example binary representation for Door (ID 1519):
 *   
 *   Offset │ Bytes       │ Value      │ Meaning
 *   ───────┼─────────────┼────────────┼─────────────────────────
 *   0x0000 │ EF 05       │ 1519       │ Object ID (little-endian)
 *   0x0002 │ 04          │ 4          │ Name length
 *   0x0003 │ 44 6F 6F 72 │ "Door"     │ Name string
 *   0x0007 │ 0F          │ 15         │ Examine length
 *   0x0008 │ 41 20 77... │ "A wooden" │ Examine string
 *   0x0017 │ 00          │ 0          │ Type (WALL)
 *   0x0018 │ 01          │ 1          │ Width
 *   0x0019 │ 01          │ 1          │ Length
 *   0x001A │ 0D          │ 0b00001101 │ Flags (solid|interactive|clipped)
 *   0x001B │ 01          │ 1          │ Model count
 *   0x001C │ 29 09       │ 2345       │ Model ID
 *   0x001E │ 00 00       │ 0          │ Model type
 *   0x0020 │ 01          │ 1          │ Action count
 *   0x0021 │ 04          │ 4          │ Action length
 *   0x0022 │ 4F 70 65 6E │ "Open"     │ Action string
 * 
 * PERFORMANCE CHARACTERISTICS:
 * 
 *   OPERATION               │ TIME COMPLEXITY │ SPACE COMPLEXITY
 *   ────────────────────────┼─────────────────┼─────────────────
 *   object_system_create    │ O(capacity)     │ O(capacity)
 *   object_system_init      │ O(defs)         │ O(defs)
 *   object_system_destroy   │ O(1)            │ O(1)
 *   object_get_definition   │ O(1)            │ O(1)
 *   object_spawn            │ O(capacity)*    │ O(1)
 *   object_despawn          │ O(1)            │ O(1)
 *   object_get_at           │ O(capacity)*    │ O(1)
 * 
 *   * Could be optimized to O(1) with free list (spawn) or
 *     O(objects_in_region) with spatial indexing (get_at)
 * 
 ******************************************************************************/

#include "object.h"
#include <stdlib.h>  /* malloc, calloc, free */
#include <string.h>  /* strcpy, memset */
#include <stdio.h>   /* printf (for debug output) */

/*******************************************************************************
 * GLOBAL OBJECT SYSTEM INSTANCE
 *******************************************************************************
 * 
 * Single global instance for the entire server.
 * Initialized in main() and used by all subsystems.
 * 
 * LIFETIME:
 *   Created: At server startup (main function)
 *   Used: Throughout server runtime
 *   Destroyed: At server shutdown (cleanup phase)
 * 
 * THREAD SAFETY:
 *   NOT thread-safe - all access must be from game thread
 *   For multi-threaded servers, protect with mutex or use per-region locks
 * 
 * EXAMPLE USAGE:
 *   int main() {
 *     g_objects = object_system_create(100000);
 *     object_system_init(g_objects);
 *     
 *     // Server main loop...
 *     
 *     object_system_destroy(g_objects);
 *     return 0;
 *   }
 ******************************************************************************/
ObjectSystem* g_objects = NULL;

/*******************************************************************************
 * LIFECYCLE MANAGEMENT
 ******************************************************************************/

/*
 * object_system_create - Allocate object system with capacity
 * 
 * ALGORITHM STEPS:
 *   1. Allocate ObjectSystem struct on heap using calloc
 *      - calloc zeros memory (initialized=false, object_count=0, etc.)
 *   2. Store capacity parameter in struct
 *   3. Allocate objects array (capacity * sizeof(GameObject))
 *   4. Check for allocation failures at each step
 *   5. Return pointer to new system, or NULL on failure
 * 
 * MEMORY ALLOCATION DIAGRAM:
 * 
 *   HEAP BEFORE:
 *   ┌─────────────────────────────────────────────────────────┐
 *   │                    Free Memory                          │
 *   └─────────────────────────────────────────────────────────┘
 * 
 *   HEAP AFTER (capacity = 100,000):
 *   ┌──────────────────────┬──────────────────────────────────┐
 *   │ ObjectSystem Struct  │   GameObject Array               │
 *   │ (29 bytes)           │   (100,000 * 25 = 2,500,000 B)   │
 *   ├──────────────────────┼──────────────────────────────────┤
 *   │ definitions: NULL    │ [0]: {id=0, ...} (FREE)          │
 *   │ definition_count: 0  │ [1]: {id=0, ...} (FREE)          │
 *   │ objects: ───────────────> [2]: {id=0, ...} (FREE)       │
 *   │ object_capacity:     │ ...                              │
 *   │   100000             │ [99999]: {id=0, ...} (FREE)      │
 *   │ object_count: 0      │                                  │
 *   │ initialized: false   │                                  │
 *   └──────────────────────┴──────────────────────────────────┘
 * 
 * ZERO-INITIALIZATION BENEFITS:
 *   calloc() zeros all bytes, which means:
 *     - All GameObject.id fields are 0 (marked as FREE)
 *     - object_count starts at 0 (no objects spawned)
 *     - initialized starts at false (need to call init)
 *     - No garbage data (prevents undefined behavior)
 * 
 * FAILURE HANDLING:
 *   If ObjectSystem allocation fails:
 *     - Return NULL immediately
 *     - Caller must check return value before using
 *   
 *   If objects array allocation fails:
 *     - Free ObjectSystem struct (prevents memory leak)
 *     - Return NULL
 *     - System is NOT partially initialized
 * 
 * COMPLEXITY: O(capacity) time (calloc zeros memory), O(capacity) space
 */
ObjectSystem* object_system_create(u32 capacity) {
    /* Allocate ObjectSystem struct on heap
     * calloc() initializes memory to zero, so:
     *   - initialized = false (0)
     *   - object_count = 0
     *   - definitions = NULL (0)
     */
    ObjectSystem* objects = calloc(1, sizeof(ObjectSystem));
    if (!objects) return NULL;  /* Out of memory - allocation failed */
    
    /* Store capacity for later reference
     * Used to check if spawn would exceed capacity
     */
    objects->object_capacity = capacity;
    
    /* Allocate objects array (pre-allocated memory pool)
     * calloc() zeros all GameObject structs, so:
     *   - All id fields are 0 (marked as FREE slots)
     *   - All positions are (0, 0, 0)
     *   - All flags are false
     */
    objects->objects = calloc(capacity, sizeof(GameObject));
    if (!objects->objects) { 
        /* Objects array allocation failed
         * Must free ObjectSystem struct to prevent memory leak
         */
        free(objects);  /* Clean up partial allocation */
        return NULL;
    }
    
    /* System is allocated but NOT initialized
     * Caller must call object_system_init() before use
     */
    objects->initialized = false;
    
    return objects;
}

/*
 * object_system_destroy - Free all memory used by object system
 * 
 * ALGORITHM STEPS:
 *   1. Check if objects pointer is NULL (safe no-op if so)
 *   2. Free definitions array (if allocated by object_system_init)
 *   3. Free objects array (allocated by object_system_create)
 *   4. Free ObjectSystem struct itself
 * 
 * MEMORY DEALLOCATION DIAGRAM:
 * 
 *   HEAP BEFORE:
 *   ┌──────────────────┬────────────────┬──────────────────────┐
 *   │ ObjectSystem     │ definitions[]  │ objects[]            │
 *   │ (29 bytes)       │ (10.5 MB)      │ (2.5 MB)             │
 *   └──────────────────┴────────────────┴──────────────────────┘
 * 
 *   HEAP AFTER:
 *   ┌─────────────────────────────────────────────────────────┐
 *   │                    Free Memory                          │
 *   └─────────────────────────────────────────────────────────┘
 * 
 * DEALLOCATION ORDER:
 *   1. definitions array (dynamically allocated in init)
 *   2. objects array (dynamically allocated in create)
 *   3. ObjectSystem struct (outermost allocation)
 * 
 *   Order doesn't technically matter (free() doesn't care), but
 *   following reverse allocation order is good practice
 * 
 * NULL POINTER SAFETY:
 *   Safe to call with NULL argument (does nothing)
 *   Safe to call if definitions is NULL (wasn't initialized)
 *   
 *   Example safe usage:
 *     ObjectSystem* sys = NULL;
 *     object_system_destroy(sys);  // Safe, does nothing
 *     
 *     sys = object_system_create(1000);
 *     // Skip init, so definitions is NULL
 *     object_system_destroy(sys);  // Safe, only frees objects and struct
 * 
 * CALLER RESPONSIBILITIES:
 *   This function ONLY frees memory. Before calling, caller should:
 *     - Send despawn packets to clients (notify of object removal)
 *     - Save object state to database (if persisting changes)
 *     - Clear references to objects (prevent dangling pointers)
 * 
 * DANGLING POINTER WARNING:
 *   After calling this function:
 *     - objects pointer is INVALID (points to freed memory)
 *     - Caller should set pointer to NULL: g_objects = NULL;
 *     - Accessing freed memory causes undefined behavior (crash likely)
 * 
 * COMPLEXITY: O(1) time (free() is constant time), O(1) space
 */
void object_system_destroy(ObjectSystem* objects) {
    /* NULL check - safe to call on NULL pointer */
    if (!objects) return;
    
    /* Free definitions array (allocated in object_system_init)
     * Check if NULL first - may not be allocated if init was never called
     */
    if (objects->definitions) {
        free(objects->definitions);
    }
    
    /* Free objects array (allocated in object_system_create)
     * Always allocated if objects != NULL (created successfully)
     */
    if (objects->objects) {
        free(objects->objects);
    }
    
    /* Free ObjectSystem struct itself (outermost allocation) */
    free(objects);
}

/*
 * object_system_init - Load object definitions from cache
 * 
 * ALGORITHM STEPS:
 *   1. Validate parameters (objects not NULL, not already initialized)
 *   2. Set definition_count (currently hardcoded to 30,000)
 *   3. Allocate definitions array (definition_count entries)
 *   4. Load definitions from cache file (TODO: not yet implemented)
 *   5. Populate example definitions (Door, Tree) for testing
 *   6. Set initialized=true
 *   7. Reset object_count=0 (ensure clean state)
 * 
 * DEFINITION LOADING PROCESS (when cache is implemented):
 * 
 *   1. Open cache file:
 *      FILE* f = fopen("./cache/objects/definitions.dat", "rb");
 *   
 *   2. Read definition count:
 *      fread(&definition_count, sizeof(u32), 1, f);
 *   
 *   3. For each definition:
 *      a. Read ID:
 *         u16 id;
 *         fread(&id, sizeof(u16), 1, f);
 *      
 *      b. Read name (length-prefixed string):
 *         u8 name_len;
 *         fread(&name_len, 1, 1, f);
 *         fread(definitions[id].name, 1, name_len, f);
 *         definitions[id].name[name_len] = '\0';  // Null-terminate
 *      
 *      c. Read examine text (same pattern as name)
 *      
 *      d. Read properties:
 *         fread(&definitions[id].type, 1, 1, f);
 *         fread(&definitions[id].width, 1, 1, f);
 *         fread(&definitions[id].length, 1, 1, f);
 *      
 *      e. Read flags (packed bits):
 *         u8 flags;
 *         fread(&flags, 1, 1, f);
 *         definitions[id].solid = (flags & 0x01) != 0;
 *         definitions[id].impenetrable = (flags & 0x02) != 0;
 *         definitions[id].interactive = (flags & 0x04) != 0;
 *         definitions[id].clipped = (flags & 0x08) != 0;
 *      
 *      f. Read models (count, then array of IDs and types)
 *      
 *      g. Read actions (count, then array of length-prefixed strings)
 *   
 *   4. Close file:
 *      fclose(f);
 * 
 * EXAMPLE DEFINITION INITIALIZATION (Door):
 * 
 *   ObjectDefinition* door = &objects->definitions[1519];
 *   
 *   Memory layout before:
 *     All bytes are zero (from calloc)
 *   
 *   After initialization:
 *   ┌─────────────────────────────────────────────────────────┐
 *   │ id = 1519                                               │
 *   │ name = "Door\0" (remaining 59 bytes are zero)           │
 *   │ examine = "A wooden door.\0" (remaining 113 bytes zero) │
 *   │ type = 0 (OBJECT_TYPE_WALL)                             │
 *   │ width = 1                                               │
 *   │ length = 1                                              │
 *   │ solid = true (1)                                        │
 *   │ impenetrable = false (0)                                │
 *   │ interactive = true (1)                                  │
 *   │ clipped = true (1)                                      │
 *   │ model_ids = {0, 0, ...} (all zero, no models set)       │
 *   │ model_types = {0, 0, ...} (all zero)                    │
 *   │ model_count = 0                                         │
 *   │ actions[0] = "Open\0" (28 bytes zero)                   │
 *   │ actions[1-4] = "" (all zeros, no action)                │
 *   └─────────────────────────────────────────────────────────┘
 * 
 * IDEMPOTENCY:
 *   NOT idempotent - calling twice returns false
 *   initialized flag prevents double initialization
 *   
 *   Scenario:
 *     object_system_init(sys);  // Returns true, succeeds
 *     object_system_init(sys);  // Returns false, already initialized
 * 
 * ERROR CASES:
 *   Returns false if:
 *     - objects is NULL (invalid parameter)
 *     - objects->initialized is true (already initialized)
 *     - definitions allocation fails (out of memory)
 *     - Cache file cannot be opened (TODO: when implemented)
 *     - Cache file has invalid format (TODO: when implemented)
 * 
 * COMPLEXITY: O(definition_count) time, O(definition_count) space
 */
bool object_system_init(ObjectSystem* objects) {
    /* Validate parameters
     * Return false if system is NULL or already initialized
     */
    if (!objects || objects->initialized) return false;
    
    /* TODO: Load definition_count from cache file
     * For now, hardcode to 30,000 (typical RuneScape object count)
     * This covers all objects from original game plus custom additions
     */
    objects->definition_count = 30000;
    
    /* Allocate definitions array
     * Size: 30,000 * 350 bytes = 10.5 MB
     * calloc() zeros all memory, so:
     *   - All IDs are 0 (invalid/unused)
     *   - All strings are empty ("\0")
     *   - All flags are false
     *   - All counts are 0
     */
    objects->definitions = calloc(objects->definition_count, sizeof(ObjectDefinition));
    
    /* Check allocation success */
    if (!objects->definitions) {
        return false;  /* Out of memory */
    }
    
    /* TODO: Load definitions from cache file
     * Pseudocode:
     *   FILE* f = fopen("./cache/objects/definitions.dat", "rb");
     *   if (!f) return false;
     *   
     *   for (int i = 0; i < definition_count; i++) {
     *     parse_definition_entry(f, &objects->definitions[i]);
     *   }
     *   
     *   fclose(f);
     * 
     * For now, hardcode a few example definitions for testing
     */
    
    /* EXAMPLE DEFINITION 1: Door (ID 1519)
     * 
     * PROPERTIES:
     *   - Type: WALL (occupies edge between tiles)
     *   - Size: 1x1 (single tile edge)
     *   - Solid: Blocks movement (closed door)
     *   - Interactive: Can be opened
     *   - Action: "Open" (primary left-click action)
     * 
     * USE CASE:
     *   Common in buildings, houses, castles
     *   Player clicks to toggle open/closed state
     *   When open: solid=false (can walk through)
     *   When closed: solid=true (blocks movement)
     */
    ObjectDefinition* door = &objects->definitions[1519];
    door->id = 1519;                        /* Unique identifier */
    strcpy(door->name, "Door");             /* Display name */
    strcpy(door->examine, "A wooden door."); /* Examine text */
    door->type = OBJECT_TYPE_WALL;          /* Edge object */
    door->width = 1;                        /* 1 tile wide */
    door->length = 1;                       /* 1 tile long */
    door->solid = true;                     /* Blocks movement */
    door->interactive = true;               /* Can be clicked */
    door->clipped = true;                   /* Blocks camera */
    strcpy(door->actions[0], "Open");       /* Primary action */
    /* Other fields remain zero (from calloc):
     *   impenetrable = false (projectiles pass through)
     *   model_ids = {0, ...} (no models set yet)
     *   model_count = 0
     *   actions[1-4] = "" (no other actions)
     */
    
    /* EXAMPLE DEFINITION 2: Tree (ID 1276)
     * 
     * PROPERTIES:
     *   - Type: INTERACTABLE (occupies full tile)
     *   - Size: 1x1 (single tile)
     *   - Solid: Blocks movement (cannot walk through tree)
     *   - Interactive: Can be chopped with axe
     *   - Action: "Chop down" (woodcutting skill)
     * 
     * USE CASE:
     *   Woodcutting skill training
     *   Player clicks with axe equipped
     *   After chopping: despawn tree, spawn logs item, spawn stump
     *   After timeout: despawn stump, respawn tree
     * 
     * LIFECYCLE:
     *   Tree (permanent) -> Stump (temporary) -> Tree (respawned)
     */
    ObjectDefinition* tree = &objects->definitions[1276];
    tree->id = 1276;                        /* Unique identifier */
    strcpy(tree->name, "Tree");             /* Display name */
    strcpy(tree->examine, "A healthy tree."); /* Examine text */
    tree->type = OBJECT_TYPE_INTERACTABLE;  /* Tile object */
    tree->width = 1;                        /* 1 tile wide */
    tree->length = 1;                       /* 1 tile long */
    tree->solid = true;                     /* Blocks movement */
    tree->interactive = true;               /* Can be clicked */
    tree->clipped = true;                   /* Blocks camera */
    strcpy(tree->actions[0], "Chop down");  /* Primary action */
    /* Other fields remain zero:
     *   impenetrable = false (arrows pass over/around)
     *   model_ids = {0, ...}
     *   model_count = 0
     *   actions[1-4] = ""
     */
    
    /* Log initialization success */
    printf("Initialized object system with %u definitions\n", objects->definition_count);
    
    /* Mark system as initialized and ready for use */
    objects->initialized = true;
    
    /* Ensure object count is zero (clean state) */
    objects->object_count = 0;
    
    return true;
}

/*
 * object_get_definition - Lookup object definition by ID
 * 
 * ALGORITHM:
 *   1. Validate parameters (objects not NULL, initialized)
 *   2. Check bounds (id < definition_count)
 *   3. Return pointer to definitions[id]
 * 
 * DIRECT INDEXING EXPLANATION:
 * 
 *   Array indexing: definitions[id]
 *   
 *   Under the hood (pointer arithmetic):
 *     ObjectDefinition* result = definitions + id;
 *   
 *   Memory address calculation:
 *     address = base_address + (id * sizeof(ObjectDefinition))
 *     
 *   Example (assuming base address 0x1000, sizeof = 350 bytes):
 *     definitions[0]    = 0x1000 + (0 * 350)    = 0x1000
 *     definitions[1]    = 0x1000 + (1 * 350)    = 0x115E
 *     definitions[1519] = 0x1000 + (1519 * 350) = 0x83A2E
 *   
 *   CPU operations:
 *     1. Multiply: id * 350 (1 cycle on modern CPUs)
 *     2. Add: base + offset (1 cycle)
 *     Total: ~2 cycles = O(1) constant time
 * 
 * BOUNDS CHECKING:
 *   Without bounds check (UNSAFE):
 *     ObjectDefinition* def = &definitions[100000];  // Out of bounds!
 *     Accesses invalid memory, causes crash or corruption
 *   
 *   With bounds check (SAFE):
 *     if (id >= definition_count) return NULL;
 *     Prevents out-of-bounds access, returns error instead
 * 
 * RETURN VALUE SEMANTICS:
 *   Returns POINTER to definition, not a copy
 *   
 *   Why pointer?
 *     - Avoids copying 350 bytes (expensive)
 *     - Allows caller to read without modification
 *     - Definition lifetime managed by ObjectSystem
 *   
 *   Caller must NOT:
 *     - Modify returned definition (breaks flyweight pattern)
 *     - Free returned pointer (causes double-free)
 *     - Store pointer beyond ObjectSystem lifetime
 * 
 * COMPARISON WITH ALTERNATIVES:
 * 
 *   CURRENT (direct indexing):
 *     Time: O(1)
 *     Space: O(definition_count) for array
 *     Wastes space: Yes (unused IDs are empty slots)
 *   
 *   HASH MAP:
 *     Time: O(1) average, O(n) worst case
 *     Space: O(actual_definitions) for map
 *     Wastes space: No (only stores used IDs)
 *     Complexity: Higher implementation complexity
 *   
 *   BINARY SEARCH:
 *     Time: O(log n)
 *     Space: O(actual_definitions) for sorted array
 *     Wastes space: No
 *     Complexity: Medium (requires keeping array sorted)
 * 
 *   For this use case, direct indexing is best:
 *     - Fastest lookup (critical for game performance)
 *     - Simplest implementation
 *     - Memory waste acceptable (~10MB for 30K definitions)
 * 
 * COMPLEXITY: O(1) time, O(1) space
 */
ObjectDefinition* object_get_definition(ObjectSystem* objects, u16 id) {
    /* Validate parameters
     * Check if system exists and is initialized
     */
    if (!objects || !objects->initialized) {
        return NULL;  /* System not ready */
    }
    
    /* Bounds check - prevent out-of-bounds array access
     * id must be less than definition_count
     * 
     * Example:
     *   definition_count = 30000
     *   id = 35000 -> INVALID (exceeds array bounds)
     *   id = 1519  -> VALID (within bounds)
     */
    if (id >= objects->definition_count) {
        return NULL;  /* ID out of range */
    }
    
    /* Direct array indexing - O(1) lookup
     * Returns pointer to definition (NOT a copy)
     * 
     * Memory access:
     *   &definitions[id] = definitions + (id * sizeof(ObjectDefinition))
     */
    return &objects->definitions[id];
}

/*******************************************************************************
 * OBJECT INSTANCE FUNCTIONS
 ******************************************************************************/

/*
 * object_spawn - Create new object instance in world
 * 
 * ALGORITHM STEPS:
 *   1. Validate parameters (system initialized, capacity not exceeded)
 *   2. Find free slot in objects array (linear search for id==0)
 *   3. Initialize GameObject fields with provided parameters
 *   4. Increment object_count
 *   5. Log spawn message (debug output)
 *   6. Return pointer to new GameObject
 * 
 * FREE SLOT SEARCH ALGORITHM:
 * 
 *   Linear search for first id==0 slot:
 *   
 *   for (i = 0; i < object_capacity; i++) {
 *     if (objects[i].id == 0) {
 *       // Found free slot at index i
 *       return &objects[i];
 *     }
 *   }
 *   // No free slots found
 *   return NULL;
 *   
 *   BEST CASE: First slot is free
 *     - Check objects[0], find id==0
 *     - Return immediately
 *     - Time: O(1)
 *   
 *   AVERAGE CASE: Object count = capacity / 2
 *     - Check objects[0..capacity/2]
 *     - Find free slot around middle
 *     - Time: O(capacity/2) = O(capacity)
 *   
 *   WORST CASE: All slots full
 *     - Check all capacity slots
 *     - Return NULL (no free slots)
 *     - Time: O(capacity)
 * 
 * OPTIMIZATION - FREE LIST:
 * 
 *   Instead of linear search, maintain stack of free indices:
 *   
 *   struct ObjectSystem {
 *     ...
 *     u32* free_list;      // Array of free indices
 *     u32 free_list_top;   // Stack pointer (next free index)
 *   };
 *   
 *   Initialization:
 *     for (i = 0; i < capacity; i++) {
 *       free_list[i] = i;  // All slots initially free
 *     }
 *     free_list_top = capacity - 1;
 *   
 *   Allocation:
 *     if (free_list_top < 0) return NULL;  // No free slots
 *     u32 index = free_list[free_list_top--];  // Pop from stack
 *     return &objects[index];  // O(1) allocation!
 *   
 *   Deallocation:
 *     free_list[++free_list_top] = index;  // Push to stack, O(1)
 *   
 *   Trade-off:
 *     - Faster: O(1) allocation vs O(n) linear search
 *     - More memory: capacity * 4 bytes for free_list
 *     - Example: 100,000 capacity = 400KB extra memory
 * 
 * POSITION INITIALIZATION:
 * 
 *   Uses position_init() helper function:
 *   
 *   void position_init(Position* pos, u32 x, u32 z, u32 height) {
 *     pos->x = x;
 *     pos->z = z;
 *     pos->height = height;
 *   }
 *   
 *   Why helper function?
 *     - Encapsulation (hides Position struct details)
 *     - Consistency (all positions initialized same way)
 *     - Maintainability (can add validation in one place)
 * 
 * TEMPORARY FLAG:
 * 
 *   Always initializes as false (permanent object)
 *   Caller can change after spawning:
 *   
 *   GameObject* stump = object_spawn(...);
 *   stump->temporary = true;
 *   stump->spawn_time = get_current_tick();
 *   
 *   Why not parameter?
 *     - Most spawns are permanent (map loading)
 *     - Temporary spawns are rare (runtime actions)
 *     - Keeps function signature simpler
 * 
 * COMPLEXITY: O(capacity) worst case for free slot search, O(1) space
 */
GameObject* object_spawn(ObjectSystem* objects, u16 object_id, u32 x, u32 z, u32 height, u8 type, u8 rotation) {
    /* Validate parameters
     * System must be initialized and not at capacity
     */
    if (!objects || !objects->initialized || objects->object_count >= objects->object_capacity) {
        return NULL;  /* Invalid parameters or capacity exceeded */
    }
    
    /* Find free slot in objects array
     * Linear search for first slot with id==0
     * 
     * OPTIMIZATION OPPORTUNITY:
     *   This is O(capacity) worst case
     *   Could be O(1) with free list data structure
     *   
     * Trade-off:
     *   - Current: Simple, no extra memory
     *   - Free list: Faster, uses capacity * 4 bytes extra
     */
    GameObject* obj = NULL;
    for (u32 i = 0; i < objects->object_capacity; i++) {
        if (objects->objects[i].id == 0) {
            /* Found free slot at index i
             * id==0 means slot is not occupied
             */
            obj = &objects->objects[i];
            break;  /* Stop searching, found what we need */
        }
    }
    
    /* Check if free slot was found
     * obj==NULL means all slots are occupied
     */
    if (!obj) {
        printf("No free object slots available\n");
        return NULL;
    }
    
    /* Initialize GameObject fields
     * 
     * FIELD INITIALIZATION ORDER:
     *   1. id - Links to definition, marks slot as OCCUPIED
     *   2. position - 3D coordinates in world
     *   3. type - Object type (wall, decoration, etc.)
     *   4. rotation - Cardinal direction
     *   5. temporary - Permanent vs runtime-spawned
     *   6. spawn_time - Timestamp for temporary objects
     */
    
    /* Set object definition ID
     * IMPORTANT: This MUST be set first
     * id==0 means FREE, any other value means OCCUPIED
     */
    obj->id = object_id;
    
    /* Initialize position using helper function
     * Sets x, z, and height fields
     */
    position_init(&obj->position, x, z, height);
    
    /* Set object type
     * Type determines spatial behavior:
     *   0 = WALL (edge object)
     *   1 = WALL_DECORATION (edge, non-solid)
     *   2 = INTERACTABLE (tile object)
     *   3 = GROUND_DECORATION (tile, non-solid)
     */
    obj->type = type;
    
    /* Set rotation (0-3 for cardinal directions)
     * 0 = WEST (0 degrees)
     * 1 = NORTH (90 degrees)
     * 2 = EAST (180 degrees)
     * 3 = SOUTH (270 degrees)
     */
    obj->rotation = rotation;
    
    /* Mark as permanent object
     * Caller can change to temporary=true if needed
     */
    obj->temporary = false;
    
    /* Initialize spawn time to 0
     * Only used for temporary objects
     * Caller should set to current_tick for temporary spawns
     */
    obj->spawn_time = 0;
    
    /* Increment object count
     * Tracks number of active (id!=0) objects
     * Used for capacity checks
     * 
     * INVARIANT: object_count == number of objects with id!=0
     */
    objects->object_count++;
    
    /* Log spawn message for debugging
     * Helps track object lifecycle in server logs
     */
    printf("Spawned object %u at (%u, %u, %u)\n", object_id, x, z, height);
    
    /* Return pointer to newly spawned object
     * Caller can use this to:
     *   - Set temporary flag and spawn_time
     *   - Add to region spatial index
     *   - Send spawn packet to nearby players
     */
    return obj;
}

/*
 * object_despawn - Remove object instance from world
 * 
 * ALGORITHM STEPS:
 *   1. Validate parameters (objects not NULL, object not NULL, object active)
 *   2. Set object->id = 0 (mark slot as FREE)
 *   3. Decrement object_count
 *   4. Log despawn message (debug output)
 * 
 * FREE SLOT MARKING:
 * 
 *   Setting id=0 is the key operation:
 *   
 *   BEFORE DESPAWN:
 *     objects[42] = {
 *       id = 1519,           // OCCUPIED (Door)
 *       position = {...},
 *       type = OBJECT_TYPE_WALL,
 *       rotation = 1,
 *       temporary = false,
 *       spawn_time = 0
 *     }
 *   
 *   AFTER DESPAWN:
 *     objects[42] = {
 *       id = 0,              // FREE (slot available)
 *       position = {...},    // Old data (ignored, will be overwritten)
 *       type = OBJECT_TYPE_WALL,
 *       rotation = 1,
 *       temporary = false,
 *       spawn_time = 0
 *     }
 *   
 *   Why not zero all fields?
 *     - Only id matters for free slot detection
 *     - Zeroing is unnecessary work (costs CPU time)
 *     - Next spawn will overwrite old data anyway
 *     - OPTIMIZATION: Skip unnecessary memory writes
 * 
 * OBJECT COUNT INVARIANT:
 * 
 *   INVARIANT: object_count == number of objects with id != 0
 *   
 *   Maintained by:
 *     - object_spawn: Increments after setting id
 *     - object_despawn: Decrements after clearing id
 *   
 *   Example state progression:
 *     Initial: object_count = 0
 *     Spawn tree: id[0] = 1276, object_count = 1
 *     Spawn door: id[1] = 1519, object_count = 2
 *     Despawn tree: id[0] = 0, object_count = 1
 *     Spawn rock: id[0] = 2345, object_count = 2
 *   
 *   Why track count?
 *     - Fast capacity check (object_count >= capacity)
 *     - Avoids counting active objects (O(n) operation)
 *     - Useful for debugging and statistics
 * 
 * CALLER RESPONSIBILITIES:
 * 
 *   This function ONLY marks slot as free. Caller must:
 *   
 *   1. Remove from spatial index:
 *      region_remove_object(region, object);
 *   
 *   2. Update collision map:
 *      ObjectDefinition* def = object_get_definition(sys, object->id);
 *      if (def->solid) {
 *        collision_remove_object(collision_map, object);
 *      }
 *   
 *   3. Send despawn packet to clients:
 *      send_object_despawn_packet(nearby_players, object);
 *   
 *   4. For temporary objects with respawn:
 *      schedule_respawn(original_object_id, position, timeout);
 * 
 * DANGLING POINTER WARNING:
 * 
 *   After despawning, object pointer is DANGEROUS:
 *   
 *   UNSAFE:
 *     GameObject* tree = object_spawn(...);
 *     object_despawn(objects, tree);
 *     printf("ID: %u\n", tree->id);  // WRONG! Reads 0 (free slot)
 *     tree->rotation = 2;             // WRONG! Modifies free slot
 *   
 *   SAFE:
 *     GameObject* tree = object_spawn(...);
 *     u16 id = tree->id;              // Save ID before despawn
 *     Position pos = tree->position;  // Save position before despawn
 *     object_despawn(objects, tree);
 *     printf("Despawned ID %u at (%u, %u)\n", id, pos.x, pos.z);
 *   
 *   Why dangerous?
 *     - Slot can be reused by next object_spawn()
 *     - Reading from freed slot gives stale data
 *     - Writing to freed slot corrupts new object
 * 
 * COMPLEXITY: O(1) time, O(1) space
 */
void object_despawn(ObjectSystem* objects, GameObject* object) {
    /* Validate parameters
     * Check system, object, and that object is actually spawned
     */
    if (!objects || !object || object->id == 0) return;
    
    /* Mark slot as FREE
     * Setting id=0 makes this slot available for reuse
     * This is the ONLY field we need to change
     */
    object->id = 0;
    
    /* Decrement active object count
     * Maintains invariant: object_count == number of id!=0 objects
     */
    objects->object_count--;
    
    /* Log despawn message for debugging
     * Note: We can still read position because it hasn't been cleared
     * (We only set id=0, other fields retain old values)
     */
    printf("Despawned object at (%u, %u)\n", object->position.x, object->position.z);
}

/*
 * object_get_at - Find object at specific position and type
 * 
 * ALGORITHM STEPS:
 *   1. Validate parameters (system initialized)
 *   2. Linear search through objects array
 *   3. For each object:
 *        a. Check if slot is occupied (id != 0)
 *        b. Check if position matches (x, z, height)
 *        c. Check if type matches
 *   4. Return first match, or NULL if none found
 * 
 * LINEAR SEARCH PATTERN:
 * 
 *   for (i = 0; i < capacity; i++) {
 *     GameObject* obj = &objects[i];
 *     
 *     // Skip free slots
 *     if (obj->id == 0) continue;
 *     
 *     // Check position match (all 3 coordinates)
 *     if (obj->position.x != x) continue;
 *     if (obj->position.z != z) continue;
 *     if (obj->position.height != height) continue;
 *     
 *     // Check type match
 *     if (obj->type != type) continue;
 *     
 *     // All conditions met - found it!
 *     return obj;
 *   }
 *   
 *   // Searched all slots, no match found
 *   return NULL;
 * 
 * POSITION MATCHING:
 * 
 *   Requires EXACT match on all 3 coordinates:
 *   
 *   Example 1 - MATCH:
 *     Search for: (3232, 3232, 0)
 *     Object at: (3232, 3232, 0)
 *     Result: MATCH (all coordinates equal)
 *   
 *   Example 2 - NO MATCH (X differs):
 *     Search for: (3232, 3232, 0)
 *     Object at: (3233, 3232, 0)
 *     Result: NO MATCH (3232 != 3233)
 *   
 *   Example 3 - NO MATCH (height differs):
 *     Search for: (3232, 3232, 0)
 *     Object at: (3232, 3232, 1)
 *     Result: NO MATCH (0 != 1, different floor)
 * 
 * TYPE FILTERING:
 * 
 *   Multiple objects can exist at same position with different types:
 *   
 *   Position (3232, 3232, 0):
 *     - Type 0 (WALL): Door on north edge
 *     - Type 2 (INTERACTABLE): Chair on tile
 *     - Type 3 (GROUND_DECORATION): Flowers on tile
 *   
 *   Caller specifies which type to find:
 *     object_get_at(sys, 3232, 3232, 0, OBJECT_TYPE_WALL)
 *       -> Returns door (type 0)
 *     object_get_at(sys, 3232, 3232, 0, OBJECT_TYPE_INTERACTABLE)
 *       -> Returns chair (type 2)
 *     object_get_at(sys, 3232, 3232, 0, OBJECT_TYPE_GROUND_DECORATION)
 *       -> Returns flowers (type 3)
 * 
 * FIRST MATCH SEMANTICS:
 * 
 *   Returns first matching object found
 *   
 *   Scenario with duplicates (should not happen in practice):
 *     objects[10] = {id=1519, type=0, pos=(3232,3232,0)}  (Door)
 *     objects[20] = {id=1520, type=0, pos=(3232,3232,0)}  (Gate)
 *   
 *   Search for type=0 at (3232,3232,0):
 *     - Finds objects[10] first (index 10 < 20)
 *     - Returns Door, never checks objects[20]
 *   
 *   Why allow duplicates?
 *     - Technically shouldn't happen (map data corruption)
 *     - But returning first match is safe behavior
 *     - Alternative: Return NULL on duplicate (too strict)
 * 
 * OPTIMIZATION - SPATIAL INDEXING:
 * 
 *   CURRENT IMPLEMENTATION (linear search):
 *     Time: O(capacity)
 *     Space: O(1) extra
 *     
 *     Problem: Must check ALL capacity slots
 *     Example: capacity=100,000, object at index 99,999
 *              Must check 99,999 slots before finding it
 *   
 *   OPTIMIZED (region-based spatial indexing):
 *     
 *     Divide world into regions (e.g., 64x64 tiles each):
 *     
 *     struct Region {
 *       GameObject* objects[1000];  // Max 1000 objects per region
 *       u32 object_count;
 *     };
 *     
 *     Region regions[256][256];  // 256x256 region grid
 *     
 *     Lookup:
 *       1. Calculate region: region_x = x / 64, region_z = z / 64
 *       2. Get region: Region* r = &regions[region_x][region_z]
 *       3. Search region: Linear search r->objects (only ~100-1000 objects)
 *     
 *     Time: O(objects_in_region) typically 100-1000
 *     Space: O(capacity) for region arrays
 *     
 *     Speedup: 100-1000x faster!
 *   
 *   OPTIMIZED (hash map):
 *     
 *     Hash function:
 *       hash = (x << 16) | (z << 8) | (height << 4) | type
 *     
 *     Hash map: hash -> list of objects
 *     
 *     Lookup:
 *       1. Calculate hash
 *       2. Get hash bucket: list = hashmap[hash % bucket_count]
 *       3. Search list: Linear search (only ~1-10 objects)
 *     
 *     Time: O(1) average, O(n) worst case (hash collision)
 *     Space: O(capacity) for hash map
 *     
 *     Speedup: 1000-10000x faster!
 * 
 * COMPLEXITY: O(capacity) worst case, O(1) space
 */
GameObject* object_get_at(ObjectSystem* objects, u32 x, u32 z, u32 height, u8 type) {
    /* Validate parameters
     * System must be initialized
     */
    if (!objects || !objects->initialized) return NULL;
    
    /* Linear search through all object slots
     * Check each slot for matching position and type
     * 
     * PERFORMANCE:
     *   Best case: First slot matches (O(1))
     *   Average case: Match at middle (O(capacity/2))
     *   Worst case: No match, check all (O(capacity))
     */
    for (u32 i = 0; i < objects->object_capacity; i++) {
        GameObject* obj = &objects->objects[i];
        
        /* Check if slot is occupied
         * id==0 means FREE slot, skip it
         * 
         * OPTIMIZATION: Early exit on free slots
         * Avoids checking position/type for empty slots
         */
        if (obj->id == 0) continue;
        
        /* Check position match (all 3 coordinates must match)
         * Uses short-circuit evaluation for efficiency:
         *   - If X doesn't match, don't check Z or height
         *   - If Z doesn't match, don't check height
         *   - Only if all 3 match do we check type
         */
        if (obj->position.x != x) continue;      /* X mismatch */
        if (obj->position.z != z) continue;      /* Z mismatch */
        if (obj->position.height != height) continue;  /* Height mismatch */
        
        /* Check type match
         * Type filtering allows multiple objects at same position
         */
        if (obj->type != type) continue;         /* Type mismatch */
        
        /* All conditions met - found matching object!
         * Return pointer to object (not a copy)
         */
        return obj;
    }
    
    /* Searched all slots, no match found
     * Either:
     *   - No object at that position
     *   - Object exists but type doesn't match
     *   - Position is out of bounds (invalid coordinates)
     */
    return NULL;
}
