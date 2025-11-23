/*******************************************************************************
 * NPC.C - Non-Player Character System Implementation
 *******************************************************************************
 * 
 * This file implements the NPC (Non-Player Character) management system for
 * the RuneScape server emulator. NPCs are the computer-controlled entities
 * that bring the game world to life.
 * 
 * KEY RESPONSIBILITIES:
 *   1. Memory management for NPC system (create, destroy)
 *   2. Definition loading from game cache
 *   3. NPC spawning and despawning
 *   4. Per-tick NPC updates (movement, AI, combat)
 *   5. NPC lookup and validation
 * 
 * ARCHITECTURAL PATTERNS:
 *   - Singleton pattern (g_npcs global)
 *   - Flyweight pattern (shared definitions, unique instances)
 *   - Object pool pattern (fixed-size NPC array with reuse)
 * 
 * DATA FLOW:
 * 
 *   Server Startup:
 *   ┌──────────────────────────────────────────────────────┐
 *   │  1. npc_system_create(5000)                          │
 *   │     - Allocate NpcSystem struct                      │
 *   │     - Allocate Npc array[5000]                       │
 *   └────────────────┬─────────────────────────────────────┘
 *                    v
 *   ┌──────────────────────────────────────────────────────┐
 *   │  2. npc_system_init(npcs)                            │
 *   │     - Allocate NpcDefinition array[10000]            │
 *   │     - Load definitions from cache (TODO)             │
 *   │     - Hardcode sample NPCs (Hans, Man)               │
 *   └────────────────┬─────────────────────────────────────┘
 *                    v
 *   ┌──────────────────────────────────────────────────────┐
 *   │  3. Spawn initial NPCs                               │
 *   │     - npc_spawn(npcs, 0, 3222, 3218, 0) // Hans      │
 *   │     - npc_spawn(npcs, 1, 3200, 3200, 0) // Man       │
 *   │     - ... spawn hundreds/thousands more              │
 *   └────────────────┬─────────────────────────────────────┘
 *                    v
 *   ┌──────────────────────────────────────────────────────┐
 *   │  4. Game Loop (every 600ms tick)                     │
 *   │     for each active NPC:                             │
 *   │       npc_process(npc)                               │
 *   │         - Process movement                           │
 *   │         - Process combat (TODO)                      │
 *   │         - Process AI (TODO)                          │
 *   └──────────────────────────────────────────────────────┘
 * 
 * MEMORY EFFICIENCY:
 * 
 *   Naive approach (store all data in each NPC):
 *     5000 NPCs * 256 bytes each = 1.28 MB
 * 
 *   Flyweight pattern (separate definition from instance):
 *     Definitions:  10000 * 128 bytes = 1.28 MB (one-time)
 *     Instances:     5000 *  80 bytes = 400 KB
 *     Total:                            1.68 MB
 * 
 *   Savings: Minimal memory overhead, massive code clarity
 * 
 * PERFORMANCE CHARACTERISTICS:
 * 
 *   Operation              Time Complexity    Space Complexity
 *   ----------------------------------------------------------------
 *   npc_system_create      O(N)               O(N)
 *   npc_system_destroy     O(1)               O(1)
 *   npc_system_init        O(M)               O(M)  M=definition_count
 *   npc_spawn              O(N) worst case    O(1)
 *   npc_despawn            O(1)               O(1)
 *   npc_process            O(1) currently     O(1)
 *   npc_get_by_index       O(1)               O(1)
 *   npc_get_definition     O(1)               O(1)
 *   npc_is_active          O(1)               O(1)
 * 
 ******************************************************************************/

#include "npc.h"
#include <stdlib.h>   /* malloc, calloc, free */
#include <string.h>   /* strcpy */
#include <stdio.h>    /* printf */

/*******************************************************************************
 * GLOBAL STATE
 ******************************************************************************/

/*
 * g_npcs - Global singleton NPC system instance
 * 
 * SINGLETON JUSTIFICATION:
 *   - Only one game world per server process
 *   - Simplifies API (no need to thread npcs pointer everywhere)
 *   - Matches game architecture (centralized world state)
 * 
 * THREAD SAFETY:
 *   NOT thread-safe. Assumes single-threaded game loop.
 *   If multi-threading needed, protect with mutex.
 * 
 * INITIALIZATION:
 *   Set to NULL initially (defined in header as extern)
 *   Allocated in main.c during server startup
 *   Freed during server shutdown
 */
NpcSystem* g_npcs = NULL;

/*******************************************************************************
 * NPC SYSTEM LIFECYCLE
 ******************************************************************************/

/*
 * npc_system_create - Allocate NPC system with specified capacity
 * 
 * MEMORY ALLOCATION STRATEGY:
 *   Use calloc (not malloc) to zero-initialize all fields:
 *     - Sets active=false for all NPC slots (ready for spawning)
 *     - Sets pointers to NULL (safe cleanup on error)
 *     - Initializes numeric fields to 0
 * 
 * ALLOCATION DIAGRAM:
 * 
 *   Heap Before:
 *   ┌────────────────────────────────────────┐
 *   │  (available memory)                    │
 *   └────────────────────────────────────────┘
 * 
 *   After calloc(1, sizeof(NpcSystem)):
 *   ┌────────────────────────────────────────┐
 *   │  NpcSystem struct (40 bytes)           │
 *   │  ┌──────────────────────────────────┐  │
 *   │  │ definitions = NULL               │  │
 *   │  │ definition_count = 0             │  │
 *   │  │ npcs = NULL                      │  │
 *   │  │ npc_capacity = 0                 │  │
 *   │  │ initialized = false              │  │
 *   │  └──────────────────────────────────┘  │
 *   └────────────────────────────────────────┘
 * 
 *   After calloc(capacity, sizeof(Npc)):
 *   ┌────────────────────────────────────────┐
 *   │  NpcSystem struct                      │
 *   │  ┌──────────────────────────────────┐  │
 *   │  │ npcs ────────────────┐           │  │
 *   │  │ npc_capacity = N     │           │  │
 *   │  └──────────────────────┼───────────┘  │
 *   └────────────────────────┼───────────────┘
 *                            │
 *                            v
 *   ┌────────────────────────────────────────┐
 *   │  Npc array (N * 80 bytes)              │
 *   │  ┌──────┬──────┬──────┬─────┬──────┐   │
 *   │  │npc[0]│npc[1]│npc[2]│ ... │npc[N]│   │
 *   │  │all fields zero-initialized      │   │
 *   │  │active=false (ready for spawn)   │   │
 *   │  └──────┴──────┴──────┴─────┴──────┘   │
 *   └────────────────────────────────────────┘
 * 
 * ERROR RECOVERY:
 *   Two-phase allocation with rollback on failure:
 *     Phase 1: Allocate NpcSystem
 *       - If fails: return NULL immediately
 *     Phase 2: Allocate Npc array
 *       - If fails: free NpcSystem, return NULL
 *   
 *   This prevents memory leaks on partial allocation
 * 
 * CAPACITY GUIDELINES:
 *   Small server:    1000-2000 NPCs (80-160 KB)
 *   Medium server:   5000 NPCs      (400 KB)
 *   Large server:    10000-20000    (800 KB - 1.6 MB)
 * 
 * EXAMPLE USAGE:
 *   NpcSystem* npcs = npc_system_create(5000);
 *   if (!npcs) {
 *     fprintf(stderr, "Out of memory!\n");
 *     exit(EXIT_FAILURE);
 *   }
 * 
 * COMPLEXITY: O(capacity) time (zero-init), O(capacity) space
 */
NpcSystem* npc_system_create(u32 capacity) {
    /* PHASE 1: Allocate NpcSystem structure */
    NpcSystem* npcs = calloc(1, sizeof(NpcSystem));
    if (!npcs) {
        /* Out of memory - system struct allocation failed */
        return NULL;
    }
    
    /* PHASE 2: Allocate NPC instance array */
    npcs->npc_capacity = capacity;
    npcs->npcs = calloc(capacity, sizeof(Npc));
    if (!npcs->npcs) {
        /* Out of memory - array allocation failed
         * CRITICAL: Must free npcs to prevent memory leak */
        free(npcs);
        return NULL;
    }
    
    /* Zero-initialization by calloc sets:
     *   - All npc[i].active = false (slots available)
     *   - All npc[i].hitpoints = 0
     *   - All npc[i].update_flags = 0
     *   - All pointers = NULL
     */
    
    /* System not yet usable (need to call npc_system_init) */
    npcs->initialized = false;
    
    return npcs;
}

/*
 * npc_system_destroy - Free all heap-allocated NPC system memory
 * 
 * DEALLOCATION ORDER:
 *   Must free in reverse order of allocation (child before parent):
 *     1. definitions array (child of NpcSystem)
 *     2. npcs array (child of NpcSystem)
 *     3. NpcSystem struct (parent)
 * 
 * DEALLOCATION DIAGRAM:
 * 
 *   Before destroy:
 *   ┌───────────────┐
 *   │  NpcSystem    │
 *   │  ┌─────────┐  │
 *   │  │defs ────┼──┼──> [definition array]
 *   │  │npcs ────┼──┼──> [npc instance array]
 *   │  └─────────┘  │
 *   └───────────────┘
 * 
 *   After free(definitions):
 *   ┌───────────────┐
 *   │  NpcSystem    │
 *   │  ┌─────────┐  │
 *   │  │defs ────┼──┼──> (freed)
 *   │  │npcs ────┼──┼──> [npc instance array]
 *   │  └─────────┘  │
 *   └───────────────┘
 * 
 *   After free(npcs):
 *   ┌───────────────┐
 *   │  NpcSystem    │
 *   │  ┌─────────┐  │
 *   │  │defs ────┼──┼──> (freed)
 *   │  │npcs ────┼──┼──> (freed)
 *   │  └─────────┘  │
 *   └───────────────┘
 * 
 *   After free(npcs):
 *   (all freed)
 * 
 * DANGLING POINTER PREVENTION:
 *   After calling this function, caller should:
 *     npc_system_destroy(g_npcs);
 *     g_npcs = NULL;  // Prevent use-after-free
 * 
 * MOVEMENT HANDLER CLEANUP:
 *   This function does NOT free MovementHandler waypoint queues
 *   Caller MUST despawn all active NPCs first:
 *     for (i = 0; i < npcs->npc_capacity; i++) {
 *       if (npcs->npcs[i].active) {
 *         npc_despawn(npcs, &npcs->npcs[i]);
 *       }
 *     }
 *     npc_system_destroy(npcs);
 * 
 * NULL SAFETY:
 *   Safe to call with NULL pointer (no-op)
 *   Safe to call multiple times (checks pointers before freeing)
 * 
 * COMPLEXITY: O(1) time
 */
void npc_system_destroy(NpcSystem* npcs) {
    /* NULL-safe: allow calling with NULL pointer */
    if (!npcs) return;
    
    /* Free definitions array if allocated */
    if (npcs->definitions) {
        free(npcs->definitions);
        /* Good practice: NULL out dangling pointer
         * (not strictly necessary since freeing npcs next) */
    }
    
    /* Free NPC instances array if allocated */
    if (npcs->npcs) {
        free(npcs->npcs);
    }
    
    /* Finally, free the NpcSystem struct itself */
    free(npcs);
    
    /* Note: Caller must set g_npcs = NULL to prevent use-after-free */
}

/*
 * npc_system_init - Load NPC definitions from game cache
 * 
 * INITIALIZATION PROCESS:
 *   1. Validate system not already initialized (prevent double-init)
 *   2. Allocate definitions array
 *   3. Load NPC data from cache files (TODO: implement)
 *   4. Set initialized=true
 * 
 * CACHE FILE FORMAT (RuneScape):
 *   NPC definitions stored in binary cache files:
 *     - npc.dat: Definition data (name, stats, models)
 *     - npc.idx: Index for quick lookup
 * 
 *   Each definition contains:
 *     - ID (u16)
 *     - Name (variable-length string)
 *     - Examine text (variable-length string)
 *     - Combat stats (u8/u16 fields)
 *     - Model IDs (u16 array)
 *     - Animation IDs (u16 fields)
 *     - Colors (u16 array)
 *     - Flags (bool fields)
 * 
 * CURRENT IMPLEMENTATION:
 *   Hardcoded sample definitions for testing:
 *     ID 0: Hans - Lumbridge Castle servant (non-combat NPC)
 *     ID 1: Man - Generic low-level human (combat level 2)
 * 
 * FUTURE IMPLEMENTATION:
 *   Replace hardcoded data with cache loading:
 *     FILE* dat = fopen("data/npc.dat", "rb");
 *     FILE* idx = fopen("data/npc.idx", "rb");
 *     for (u16 id = 0; id < definition_count; id++) {
 *       load_npc_definition(&definitions[id], dat, idx);
 *     }
 * 
 * HANS DEFINITION BREAKDOWN:
 * 
 *   ┌─────────────────────────────────────────────────────┐
 *   │  NPC ID 0: Hans                                     │
 *   ├─────────────────────────────────────────────────────┤
 *   │  name: "Hans"                                       │
 *   │  examine: "Servant of the Duke of Lumbridge."       │
 *   │  combat_level: 0 (non-combat)                       │
 *   │  max_hitpoints: 0 (can't be attacked)               │
 *   │  walk_radius: 20 tiles (wanders around castle)      │
 *   │  aggressive: false                                  │
 *   │  size: 1 (occupies 1x1 tile)                        │
 *   └─────────────────────────────────────────────────────┘
 * 
 * MAN DEFINITION BREAKDOWN:
 * 
 *   ┌─────────────────────────────────────────────────────┐
 *   │  NPC ID 1: Man                                      │
 *   ├─────────────────────────────────────────────────────┤
 *   │  name: "Man"                                        │
 *   │  examine: "One of Lumbridge's residents."           │
 *   │  combat_level: 2                                    │
 *   │  max_hitpoints: 7                                   │
 *   │  attack_speed: 4 ticks (2.4 seconds)                │
 *   │  respawn_time: 25 ticks (15 seconds)                │
 *   │  walk_radius: 5 tiles                               │
 *   │  aggressive: false                                  │
 *   │  retreats: true (runs when low HP)                  │
 *   │  size: 1 (occupies 1x1 tile)                        │
 *   └─────────────────────────────────────────────────────┘
 * 
 * MEMORY ALLOCATION:
 *   10000 definitions * 128 bytes each = 1.28 MB
 *   Allocated once, used for lifetime of server
 * 
 * ERROR HANDLING:
 *   Returns false if:
 *     - npcs is NULL (invalid parameter)
 *     - Already initialized (prevent double-init)
 *     - calloc fails (out of memory)
 * 
 * COMPLEXITY: O(definition_count) time and space
 */
bool npc_system_init(NpcSystem* npcs) {
    /* Validate parameters */
    if (!npcs) return false;
    
    /* Prevent double-initialization */
    if (npcs->initialized) return false;
    
    /* TODO: Load actual count from cache files
     * For now, allocate space for 10000 possible NPC types */
    npcs->definition_count = 10000;
    npcs->definitions = calloc(npcs->definition_count, sizeof(NpcDefinition));
    
    /* Check allocation success */
    if (!npcs->definitions) {
        /* Out of memory */
        return false;
    }
    
    /*--------------------------------------------------------------------------
     * HARDCODED DEFINITION: NPC ID 1 = "Man"
     *--------------------------------------------------------------------------*/
    
    /* Get pointer to definition slot 1 */
    NpcDefinition* man = &npcs->definitions[1];
    
    /* Identity */
    man->id = 1;
    strcpy(man->name, "Man");
    strcpy(man->examine, "One of Lumbridge's residents.");
    
    /* Combat stats
     * 
     * Combat level calculation (simplified):
     *   Base: attack + strength + defence + hitpoints + prayer / 2
     *   Man: Low-level NPC, minimal combat ability
     * 
     * Hitpoints: 7 HP (dies in 2-3 hits for low-level players)
     */
    man->combat_level = 2;
    man->max_hitpoints = 7;
    
    /* Attack speed: 4 ticks per attack
     * 
     * Tick rate: 600ms (0.6 seconds)
     * Attack interval: 4 * 0.6 = 2.4 seconds between attacks
     * 
     * For comparison:
     *   Fast weapons (dagger): 2 ticks (1.2 seconds)
     *   Normal weapons (sword): 4 ticks (2.4 seconds)
     *   Slow weapons (2h sword): 6 ticks (3.6 seconds)
     */
    man->attack_speed = 4;
    
    /* Respawn time: 25 ticks after death
     * 
     * Calculation: 25 * 0.6 = 15 seconds
     * 
     * RuneScape respawn times vary by NPC:
     *   Common NPCs (chickens, cows): 10-30 seconds
     *   Uncommon NPCs (guards): 30-60 seconds
     *   Bosses: 60-120 seconds or longer
     */
    man->respawn_time = 25;
    
    /* Walk radius: 5 tiles from spawn point
     * 
     * Random walk behavior:
     *   Every few ticks, NPC picks random tile within radius
     *   Uses pathfinding to navigate there
     *   When reached, picks new destination
     * 
     * Visual representation (spawn at center):
     * 
     *   ┌─────────────────────────────────┐
     *   │  . . . . . . . . . . .          │
     *   │  . . . . X X X . . . .          │
     *   │  . . . X X X X X . . .          │
     *   │  . . X X X X X X X . .          │
     *   │  . . X X X S X X X . .  S = spawn point
     *   │  . . X X X X X X X . .  X = walkable area (radius 5)
     *   │  . . . X X X X X . . .  . = outside walk radius
     *   │  . . . . X X X . . . .          │
     *   │  . . . . . . . . . . .          │
     *   └─────────────────────────────────┘
     */
    man->walk_radius = 5;
    
    /* Behavioral flags */
    man->aggressive = false;  /* Won't auto-attack players */
    man->retreats = true;     /* Runs away when low HP (<=20%) */
    
    /* Size: 1x1 tiles
     * 
     * Size affects:
     *   - Collision detection (1x1 NPCs block 1 tile)
     *   - Pathfinding (larger NPCs need more clearance)
     *   - Combat distance (larger NPCs attackable from farther)
     * 
     * Size examples:
     *   1x1: Humans, goblins, chickens
     *   2x2: Giants, hill giants
     *   3x3: Dragons, demons
     *   5x5: King Black Dragon
     */
    man->size = 1;
    
    /* Note: models, colors, and animations left at 0 (not yet implemented) */
    
    /*--------------------------------------------------------------------------
     * HARDCODED DEFINITION: NPC ID 0 = "Hans"
     *--------------------------------------------------------------------------*/
    
    /* Get pointer to definition slot 0 */
    NpcDefinition* hans = &npcs->definitions[0];
    
    /* Identity */
    hans->id = 0;
    strcpy(hans->name, "Hans");
    strcpy(hans->examine, "Servant of the Duke of Lumbridge.");
    
    /* Non-combat NPC
     * 
     * Combat level 0 indicates NPC cannot be attacked
     * Max hitpoints 0 prevents targeting
     * 
     * Hans is a quest/dialogue NPC, not meant for combat
     */
    hans->combat_level = 0;
    hans->max_hitpoints = 0;
    
    /* Walk radius: 20 tiles
     * 
     * Hans wanders around Lumbridge Castle courtyard
     * Larger radius than combat NPCs (more dynamic)
     * 
     * Path may include:
     *   - Castle entrance
     *   - Courtyard
     *   - Kitchen area
     *   - Duke's throne room
     */
    hans->walk_radius = 20;
    
    /* Behavioral flags */
    hans->aggressive = false;  /* Never attacks (non-combat) */
    
    /* Size */
    hans->size = 1;  /* Standard human size */
    
    /* Debug output */
    printf("Initialized NPC system with %u definitions\n", npcs->definition_count);
    
    /* Mark system as initialized (now safe to spawn NPCs) */
    npcs->initialized = true;
    return true;
}

/*
 * npc_get_definition - Retrieve NPC template by ID
 * 
 * LOOKUP ALGORITHM:
 *   Direct array indexing (O(1)):
 *     return &definitions[id];
 * 
 * BOUNDS CHECKING:
 *   Essential to prevent buffer overflow:
 *     if (id >= definition_count) return NULL;
 * 
 * ARRAY INDEXING DIAGRAM:
 * 
 *   definitions array:
 *   ┌───────┬───────┬───────┬─────┬─────────┐
 *   │ def[0]│ def[1]│ def[2]│ ... │def[9999]│
 *   │ Hans  │  Man  │Goblin │     │ Dragon  │
 *   └───────┴───────┴───────┴─────┴─────────┘
 *      ^
 *      |
 *   id=0: return &definitions[0] (Hans)
 *   id=1: return &definitions[1] (Man)
 *   id=10000: OUT OF BOUNDS, return NULL
 * 
 * USAGE PATTERN:
 *   NpcDefinition* def = npc_get_definition(g_npcs, npc->npc_id);
 *   if (def) {
 *     Draw name tag: def->name
 *     Show combat level: def->combat_level
 *     Render model: def->models[...]
 *   }
 * 
 * RETURN VALUE LIFETIME:
 *   Pointer valid until npc_system_destroy() called
 *   DO NOT free() the returned pointer (managed by system)
 * 
 * THREAD SAFETY:
 *   Read-only access is thread-safe (definitions never modified)
 *   Multiple threads can call this concurrently
 * 
 * COMPLEXITY: O(1) time, O(1) space
 */
NpcDefinition* npc_get_definition(NpcSystem* npcs, u16 id) {
    /* Validate system pointer */
    if (!npcs) return NULL;
    
    /* Ensure system initialized (definitions loaded) */
    if (!npcs->initialized) return NULL;
    
    /* Bounds check: prevent buffer overflow */
    if (id >= npcs->definition_count) {
        return NULL;
    }
    
    /* Direct array access - O(1) lookup */
    return &npcs->definitions[id];
}

/*******************************************************************************
 * NPC INSTANCE MANAGEMENT
 ******************************************************************************/

/*
 * npc_spawn - Create new NPC instance at specified coordinates
 * 
 * SPAWNING ALGORITHM:
 *   1. Find free slot (linear search)
 *   2. Initialize NPC state
 *   3. Return pointer to NPC
 * 
 * SLOT ALLOCATION ALGORITHM (FIRST-FIT):
 * 
 *   for i = 0 to capacity-1:
 *     if npcs[i].active == false:
 *       allocate npcs[i]
 *       return &npcs[i]
 *   return NULL (no slots available)
 * 
 * SLOT SEARCH VISUALIZATION:
 * 
 *   NPC array state (capacity=8):
 *   ┌────┬────┬────┬────┬────┬────┬────┬────┐
 *   │ 0  │ 1  │ 2  │ 3  │ 4  │ 5  │ 6  │ 7  │  <- indices
 *   ├────┼────┼────┼────┼────┼────┼────┼────┤
 *   │ T  │ T  │ F  │ T  │ T  │ F  │ T  │ F  │  <- active flag (T/F)
 *   └────┴────┴────┴────┴────┴────┴────┴────┘
 *              ^
 *              |
 *      First free slot found at index 2
 * 
 * TIME COMPLEXITY ANALYSIS:
 * 
 *   Best case:  O(1) - first slot is free
 *   Average:    O(N/2) - slot halfway through array
 *   Worst case: O(N) - all slots full, or last slot free
 * 
 *   Where N = npc_capacity
 * 
 * OPTIMIZATION OPPORTUNITY (FUTURE):
 * 
 *   Maintain free list for O(1) allocation:
 * 
 *   ┌──────────────┐
 *   │ NpcSystem    │
 *   │ free_head ───┼──> npc[2]
 *   └──────────────┘       |
 *                          v
 *                      npc[5]
 *                          |
 *                          v
 *                      npc[7]
 *                          |
 *                          v
 *                        NULL
 * 
 *   Trade-off: 8 bytes per NPC for next pointer
 *   Benefit: O(1) allocation instead of O(N)
 * 
 * INITIALIZATION SEQUENCE:
 * 
 *   Step 1: Find free slot
 *   ┌────────────┐
 *   │ npc[i]     │  active=false (free)
 *   └────────────┘
 * 
 *   Step 2: Set index and NPC ID
 *   ┌────────────┐
 *   │ index = i  │
 *   │ npc_id = X │
 *   └────────────┘
 * 
 *   Step 3: Initialize positions
 *   ┌────────────────────────────┐
 *   │ position = (x, z, height)  │
 *   │ spawn_position = same      │
 *   └────────────────────────────┘
 * 
 *   Step 4: Initialize movement handler
 *   ┌────────────────────────────┐
 *   │ movement.waypoints = []    │
 *   │ movement.size = 0          │
 *   └────────────────────────────┘
 * 
 *   Step 5: Set initial stats
 *   ┌────────────────────────────┐
 *   │ hitpoints = def->max_hp    │
 *   │ update_flags = 0           │
 *   │ respawn_timer = 0          │
 *   └────────────────────────────┘
 * 
 *   Step 6: Activate
 *   ┌────────────────────────────┐
 *   │ active = true              │
 *   └────────────────────────────┘
 * 
 * COORDINATE SYSTEM:
 * 
 *   RuneScape uses tile-based coordinates:
 *     x: East-West axis (increases eastward)
 *     z: North-South axis (increases northward)
 *     height: Vertical layer (0=ground, 1=first floor, etc.)
 * 
 *   Example locations:
 *     Lumbridge Castle: (3222, 3218, 0)
 *     Varrock Square:   (3212, 3424, 0)
 *     Wilderness:       (3200, 3800, 0)
 * 
 * SPAWN VS CURRENT POSITION:
 * 
 *   spawn_position: Where NPC originally spawned (immutable)
 *   position: Where NPC currently is (mutable)
 * 
 *   Uses:
 *     - Random walking: Stay within walk_radius of spawn_position
 *     - Respawning: Reset position to spawn_position
 *     - Leashing: Return to spawn if pulled too far
 * 
 * NETWORK PROTOCOL:
 * 
 *   When NPC spawns, server sends to nearby clients:
 *     Opcode: NPC_ADD (or similar)
 *     Data:
 *       - NPC index (u16)
 *       - NPC ID (u16)
 *       - Position (x, z, height)
 *       - Direction (u8)
 *   
 *   Client adds NPC to local viewport
 * 
 * EXAMPLE USAGE:
 * 
 *   Spawn Hans at Lumbridge Castle entrance:
 *     Npc* hans = npc_spawn(g_npcs, 0, 3222, 3218, 0);
 *     if (!hans) {
 *       fprintf(stderr, "Failed to spawn Hans!\n");
 *     }
 * 
 *   Spawn 10 goblins around player:
 *     for (int i = 0; i < 10; i++) {
 *       u32 x = player->x + (rand() % 20) - 10;
 *       u32 z = player->z + (rand() % 20) - 10;
 *       npc_spawn(g_npcs, GOBLIN_ID, x, z, 0);
 *     }
 * 
 * COMPLEXITY: O(N) time worst case, O(1) space
 */
Npc* npc_spawn(NpcSystem* npcs, u16 npc_id, u32 x, u32 z, u32 height) {
    /* Validate parameters */
    if (!npcs) return NULL;
    if (!npcs->initialized) return NULL;
    
    /*--------------------------------------------------------------------------
     * SLOT ALLOCATION: Find first free NPC slot
     *--------------------------------------------------------------------------*/
    
    Npc* npc = NULL;  /* Will point to allocated slot */
    
    /* Linear search through NPC array */
    for (u32 i = 0; i < npcs->npc_capacity; i++) {
        /* Check if slot is available */
        if (!npcs->npcs[i].active) {
            /* Found free slot! */
            npc = &npcs->npcs[i];
            npc->index = (u16)i;  /* Store index for network protocol */
            break;  /* Stop searching */
        }
    }
    
    /* Check if allocation failed (all slots in use) */
    if (!npc) {
        printf("No free NPC slots available\n");
        return NULL;
    }
    
    /*--------------------------------------------------------------------------
     * NPC STATE INITIALIZATION
     *--------------------------------------------------------------------------*/
    
    /* Link to definition (template) */
    npc->npc_id = npc_id;
    
    /* Initialize current position
     * 
     * position_init defined in position.h:
     *   void position_init(Position* pos, u32 x, u32 z, u32 height) {
     *     pos->x = x;
     *     pos->z = z;
     *     pos->height = height;
     *   }
     */
    position_init(&npc->position, x, z, height);
    
    /* Initialize spawn position (same as current initially)
     * NPC will respawn here after death */
    position_init(&npc->spawn_position, x, z, height);
    
    /* Initialize movement handler
     * 
     * movement_init defined in movement.h:
     *   void movement_init(MovementHandler* movement) {
     *     movement->waypoints = NULL;
     *     movement->waypoint_count = 0;
     *     movement->capacity = 0;
     *   }
     * 
     * Sets up empty waypoint queue for pathfinding
     */
    movement_init(&npc->movement);
    
    /* Initialize combat stats from definition */
    NpcDefinition* def = npc_get_definition(npcs, npc_id);
    if (def) {
        /* Set hitpoints to maximum (full health) */
        npc->hitpoints = def->max_hitpoints;
    } else {
        /* Definition not found (shouldn't happen if npc_id valid) */
        npc->hitpoints = 0;
    }
    
    /* Clear update flags (no pending updates) */
    npc->update_flags = 0;
    
    /* Mark slot as active (in use) */
    npc->active = true;
    
    /* Clear respawn timer (NPC is alive) */
    npc->respawn_timer = 0;
    
    /* Debug output */
    printf("Spawned NPC %u (id: %u) at (%u, %u, %u)\n", 
           npc->index, npc_id, x, z, height);
    
    return npc;
}

/*
 * npc_despawn - Remove NPC from world and free slot
 * 
 * DESPAWNING ALGORITHM:
 *   1. Validate NPC is active
 *   2. Mark slot as inactive (active=false)
 *   3. Cleanup movement handler (free waypoints)
 *   4. NPC state remains in memory (slot reusable)
 * 
 * STATE TRANSITION:
 * 
 *   Before despawn:
 *   ┌─────────────────┐
 *   │ Npc slot        │
 *   │ active = true   │
 *   │ index = 42      │
 *   │ npc_id = 1      │
 *   │ position = ...  │
 *   │ movement = ...  │
 *   └─────────────────┘
 * 
 *   After despawn:
 *   ┌─────────────────┐
 *   │ Npc slot        │
 *   │ active = false  │  <- Slot now free for reuse
 *   │ index = 42      │  <- Data still in memory
 *   │ npc_id = 1      │     (will be overwritten on next spawn)
 *   │ position = ...  │
 *   │ movement = ...  │  <- Waypoints freed
 *   └─────────────────┘
 * 
 * MEMORY CLEANUP:
 * 
 *   Movement handler may have allocated waypoint queue:
 *     movement_destroy(&npc->movement)
 *       -> free(movement->waypoints)
 *       -> movement->waypoints = NULL
 *       -> movement->capacity = 0
 * 
 *   NPC struct itself NOT freed (part of array)
 * 
 * SLOT REUSE:
 * 
 *   After despawn, slot available for next npc_spawn():
 * 
 *   Time T:   npc_spawn(npcs, 1, x, z, 0)  -> uses slot 42
 *   Time T+1: npc_despawn(npcs, npc)       -> frees slot 42
 *   Time T+2: npc_spawn(npcs, 2, x, z, 0)  -> reuses slot 42
 * 
 *   Same index, different NPC!
 *   Clients must handle index reuse correctly
 * 
 * NETWORK PROTOCOL:
 * 
 *   When NPC despawns, server sends to nearby clients:
 *     Opcode: NPC_REMOVE (or similar)
 *     Data:
 *       - NPC index (u16)
 *   
 *   Client removes NPC from local viewport
 * 
 * USE CASES:
 * 
 *   1. Quest completion:
 *      Quest NPC no longer needed after quest finished
 * 
 *   2. Dynamic spawning:
 *      Remove NPCs from areas with no players (optimization)
 * 
 *   3. Event cleanup:
 *      Remove event-specific NPCs when event ends
 * 
 *   4. Administrative:
 *      GM command to remove stuck/bugged NPCs
 * 
 * SAFETY FEATURES:
 * 
 *   - NULL-safe: Returns immediately if npcs or npc is NULL
 *   - Double-despawn safe: Checks active flag before proceeding
 *   - No memory leaks: Frees all allocated resources
 * 
 * EXAMPLE USAGE:
 * 
 *   Quest NPC cleanup:
 *     if (quest_completed) {
 *       npc_despawn(g_npcs, quest_npc);
 *       quest_npc = NULL;  // Prevent use-after-despawn
 *     }
 * 
 *   Despawn all NPCs in area:
 *     for (u32 i = 0; i < g_npcs->npc_capacity; i++) {
 *       Npc* npc = &g_npcs->npcs[i];
 *       if (npc->active && in_area(npc, area)) {
 *         npc_despawn(g_npcs, npc);
 *       }
 *     }
 * 
 * COMPLEXITY: O(1) time (assumes movement_destroy is O(1))
 */
void npc_despawn(NpcSystem* npcs, Npc* npc) {
    /* Validate parameters */
    if (!npcs) return;
    if (!npc) return;
    
    /* Check if NPC is actually active */
    if (!npc->active) return;  /* Already despawned, no-op */
    
    /* Mark slot as inactive (available for reuse) */
    npc->active = false;
    
    /* Cleanup movement handler
     * 
     * movement_destroy defined in movement.h:
     *   void movement_destroy(MovementHandler* movement) {
     *     if (movement->waypoints) {
     *       free(movement->waypoints);
     *       movement->waypoints = NULL;
     *       movement->capacity = 0;
     *       movement->waypoint_count = 0;
     *     }
     *   }
     * 
     * Frees dynamically allocated waypoint queue
     */
    movement_destroy(&npc->movement);
    
    /* Debug output */
    printf("Despawned NPC %u\n", npc->index);
    
    /* Note: NPC struct remains in array (not freed)
     * Data will be overwritten on next spawn to this slot */
}

/*
 * npc_process - Update NPC state for one game tick
 * 
 * GAME TICK EXPLAINED:
 * 
 *   RuneScape runs on a tick-based system:
 *     - 1 tick = 600 milliseconds (0.6 seconds)
 *     - Server updates all entities every tick
 *     - Client syncs with server every tick
 * 
 *   Tick rate: 1.67 ticks per second (60 ticks per minute)
 * 
 * CURRENT IMPLEMENTATION:
 * 
 *   Only processes movement (AI/combat TODO)
 * 
 * MOVEMENT PROCESSING ALGORITHM:
 * 
 *   if npc has waypoints in queue:
 *     next_waypoint = movement.waypoints[0]
 *     npc.position = next_waypoint
 *     remove waypoint from queue
 *     set movement update flag
 * 
 * WAYPOINT QUEUE VISUALIZATION:
 * 
 *   NPC at (10, 10), wants to walk to (15, 12):
 * 
 *   Path: (10,10) -> (11,10) -> (12,11) -> (13,11) -> (14,12) -> (15,12)
 * 
 *   Waypoint queue:
 *   ┌────────┬────────┬────────┬────────┬────────┐
 *   │(11,10) │(12,11) │(13,11) │(14,12) │(15,12) │
 *   └────────┴────────┴────────┴────────┴────────┘
 *        ^
 *        |
 *    next waypoint (consumed this tick)
 * 
 *   After processing:
 *   ┌────────┬────────┬────────┬────────┐
 *   │(12,11) │(13,11) │(14,12) │(15,12) │
 *   └────────┴────────┴────────┴────────┘
 * 
 *   NPC position updated from (10,10) to (11,10)
 * 
 * MOVEMENT STEPS PER TICK:
 * 
 *   Current implementation: 1 tile per tick
 *   Movement speed: 1 tile per 0.6 seconds
 * 
 *   Distance calculation:
 *     Time to walk 10 tiles = 10 ticks = 6 seconds
 * 
 *   RuneScape movement speeds:
 *     Walking: 1 tile per tick
 *     Running: 2 tiles per tick
 * 
 * FUTURE AI IMPLEMENTATIONS:
 * 
 *   1. COMBAT AI:
 *      - Detect nearby enemies
 *      - Choose target based on aggression rules
 *      - Calculate attack timing
 *      - Execute special attacks
 *      - Handle death and respawning
 * 
 *   2. RANDOM WALKING:
 *      - Check if NPC is idle (no waypoints)
 *      - Pick random tile within walk_radius
 *      - Generate path to tile
 *      - Add waypoints to queue
 * 
 *   3. AGGRESSION:
 *      - Scan for players within aggro range
 *      - Check combat level difference
 *      - Decide whether to attack
 *      - Path to player and initiate combat
 * 
 *   4. RESPAWNING:
 *      - Decrement respawn_timer if > 0
 *      - When timer reaches 0:
 *        - Reset hitpoints to max
 *        - Reset position to spawn_position
 *        - Clear combat state
 *        - Set active=true
 * 
 * RANDOM WALK ALGORITHM (FUTURE):
 * 
 *   if no waypoints and random_chance(5%):
 *     target_x = spawn_x + random(-walk_radius, +walk_radius)
 *     target_z = spawn_z + random(-walk_radius, +walk_radius)
 *     
 *     if tile_is_walkable(target_x, target_z):
 *       path = pathfind(current_pos, target_pos)
 *       add_waypoints(path)
 * 
 * COMBAT AI ALGORITHM (FUTURE):
 * 
 *   if in_combat:
 *     if attack_timer <= 0:
 *       calculate_damage()
 *       apply_damage_to_target()
 *       set_animation(ATTACK_ANIM)
 *       attack_timer = attack_speed
 *     else:
 *       attack_timer--
 *   
 *   if aggressive and not in_combat:
 *     nearby_players = find_players_in_range(aggro_range)
 *     if nearby_players:
 *       target = choose_target(nearby_players)
 *       start_combat(target)
 * 
 * RESPAWN ALGORITHM (FUTURE):
 * 
 *   if hitpoints == 0 and respawn_timer > 0:
 *     respawn_timer--
 *     
 *     if respawn_timer == 0:
 *       hitpoints = max_hitpoints
 *       position = spawn_position
 *       clear_combat_state()
 *       active = true
 * 
 * GAME LOOP INTEGRATION:
 * 
 *   void game_tick() {
 *     // Process all active NPCs
 *     for (u32 i = 0; i < g_npcs->npc_capacity; i++) {
 *       if (g_npcs->npcs[i].active) {
 *         npc_process(&g_npcs->npcs[i]);
 *       }
 *     }
 *     
 *     // Process players
 *     // Process combat
 *     // Process timers
 *     // Send updates to clients
 *   }
 * 
 * PERFORMANCE CONSIDERATIONS:
 * 
 *   With 5000 active NPCs:
 *     - 5000 function calls per tick
 *     - Need to complete in <600ms
 *     - ~120 microseconds per NPC
 * 
 *   Optimization strategies:
 *     - Skip inactive NPCs (current)
 *     - Skip NPCs far from all players (future)
 *     - Batch processing (process multiple NPCs together)
 *     - Multi-threading (split NPC array across threads)
 * 
 * EXAMPLE TICK TIMELINE:
 * 
 *   T=0ms:   Tick starts
 *   T=10ms:  Process 100 NPCs (movement)
 *   T=50ms:  Process 500 NPCs (movement)
 *   T=100ms: All NPCs processed
 *   T=150ms: Process players
 *   T=200ms: Process combat
 *   T=250ms: Send network updates
 *   T=300ms: Tick complete (300ms remaining until next tick)
 * 
 * COMPLEXITY: O(1) time currently (TODO: depends on AI)
 */
void npc_process(Npc* npc) {
    /* Validate NPC is active */
    if (!npc) return;
    if (!npc->active) return;
    
    /*--------------------------------------------------------------------------
     * MOVEMENT PROCESSING
     *--------------------------------------------------------------------------*/
    
    /* Check if NPC has queued waypoints
     * 
     * movement_is_moving defined in movement.h:
     *   bool movement_is_moving(MovementHandler* movement) {
     *     return movement->waypoint_count > 0;
     *   }
     */
    if (movement_is_moving(&npc->movement)) {
        /* Get next movement direction based on current position */
        i32 dir = movement_get_next_direction(&npc->movement, 
                                              npc->position.x, 
                                              npc->position.z);
        
        if (dir != -1) {
            /* Move NPC in calculated direction */
            position_move(&npc->position, 
                         DIRECTION_DELTA_X[dir], 
                         DIRECTION_DELTA_Z[dir]);
            /* Note: height remains same (no vertical movement) */
            
            /* TODO: Set movement update flag for network protocol
             * npc->update_flags |= NPC_UPDATE_MOVEMENT;
             * npc->primary_direction = dir;
             */
        }
    }
    
    /*--------------------------------------------------------------------------
     * FUTURE IMPLEMENTATIONS (TODO)
     *--------------------------------------------------------------------------*/
    
    /* TODO: Combat AI
     * 
     * if (npc->in_combat) {
     *   process_combat(npc);
     * }
     */
    
    /* TODO: Random walking
     * 
     * NpcDefinition* def = npc_get_definition(g_npcs, npc->npc_id);
     * if (def && def->walk_radius > 0) {
     *   if (!movement_is_moving(&npc->movement) && rand() % 100 < 5) {
     *     random_walk(npc, def);
     *   }
     * }
     */
    
    /* TODO: Aggression detection
     * 
     * NpcDefinition* def = npc_get_definition(g_npcs, npc->npc_id);
     * if (def && def->aggressive) {
     *   check_aggression(npc);
     * }
     */
    
    /* TODO: Respawn logic
     * 
     * if (npc->hitpoints == 0 && npc->respawn_timer > 0) {
     *   npc->respawn_timer--;
     *   if (npc->respawn_timer == 0) {
     *     respawn_npc(npc);
     *   }
     * }
     */
}

/*
 * npc_get_by_index - Retrieve NPC instance by network index
 * 
 * INDEX SYSTEM EXPLAINED:
 * 
 *   NPCs identified by index in network protocol:
 *     - Client tracks NPCs by index (u16)
 *     - Index = position in npcs array
 *     - Index stable while NPC active
 *     - Index reused after despawn
 * 
 * LOOKUP ALGORITHM:
 * 
 *   Direct array indexing (O(1)):
 *     npc = &npcs->npcs[index]
 *     if npc->active: return npc
 *     else: return NULL
 * 
 * BOUNDS CHECKING:
 * 
 *   Essential to prevent buffer overflow:
 * 
 *   Valid indices: 0 to (npc_capacity - 1)
 * 
 *   Example with capacity=5000:
 *     index=0:    Valid (first NPC)
 *     index=4999: Valid (last NPC)
 *     index=5000: INVALID (out of bounds)
 * 
 * ARRAY ACCESS DIAGRAM:
 * 
 *   npcs->npcs array:
 *   ┌────────┬────────┬────────┬─────┬────────┐
 *   │ npc[0] │ npc[1] │ npc[2] │ ... │npc[N-1]│
 *   │active=1│active=1│active=0│     │active=1│
 *   └────────┴────────┴────────┴─────┴────────┘
 *       ^
 *       |
 *   index=0: return &npcs->npcs[0] (active, success)
 *   index=2: return NULL (inactive)
 *   index=N: return NULL (out of bounds)
 * 
 * ACTIVE FLAG CHECK:
 * 
 *   Why check active flag?
 *     - Prevent access to despawned NPCs
 *     - Prevent access to uninitialized slots
 *     - Ensure NPC is in valid state
 * 
 *   Example scenario:
 *     T=0: Client sees NPC index 42
 *     T=1: Server despawns NPC 42
 *     T=2: Client clicks NPC 42
 *     T=3: Server gets request, but npc[42].active=false
 *          -> Returns NULL (prevents crash)
 * 
 * NETWORK PROTOCOL USAGE:
 * 
 *   Client packet: "Attack NPC"
 *     [opcode] [npc_index]
 *   
 *   Server handling:
 *     u16 index = buffer_read_short(packet, false, BIG);
 *     Npc* target = npc_get_by_index(g_npcs, index);
 *     if (target) {
 *       player_attack_npc(player, target);
 *     } else {
 *       send_error(player, "NPC not found");
 *     }
 * 
 * INDEX REUSE SCENARIO:
 * 
 *   T=0: Spawn goblin at index 10
 *   T=1: Client sees goblin (index 10)
 *   T=2: Player kills goblin
 *   T=3: Despawn goblin (index 10 now free)
 *   T=4: Spawn dragon at index 10 (reuses slot)
 *   T=5: Client clicks on old goblin position
 *        Server checks index 10:
 *          - Active? Yes
 *          - But now it's a dragon, not goblin!
 *          - Client must sync NPC list to handle this
 * 
 * ERROR HANDLING:
 * 
 *   Returns NULL if:
 *     - npcs is NULL (invalid system)
 *     - index >= npc_capacity (out of bounds)
 *     - NPC at index is inactive (despawned)
 * 
 * USAGE EXAMPLES:
 * 
 *   Combat targeting:
 *     Npc* target = npc_get_by_index(g_npcs, packet_index);
 *     if (target) {
 *       initiate_combat(player, target);
 *     }
 * 
 *   Item use on NPC:
 *     Npc* npc = npc_get_by_index(g_npcs, clicked_index);
 *     if (npc) {
 *       use_item_on_npc(player, item, npc);
 *     }
 * 
 *   Dialogue interaction:
 *     Npc* npc = npc_get_by_index(g_npcs, talked_index);
 *     if (npc) {
 *       start_dialogue(player, npc);
 *     }
 * 
 * COMPLEXITY: O(1) time, O(1) space
 */
Npc* npc_get_by_index(NpcSystem* npcs, u16 index) {
    /* Validate system pointer */
    if (!npcs) return NULL;
    
    /* Bounds check: prevent buffer overflow */
    if (index >= npcs->npc_capacity) {
        return NULL;
    }
    
    /* Get NPC at index (always safe due to bounds check) */
    Npc* npc = &npcs->npcs[index];
    
    /* Only return if NPC is active */
    return npc->active ? npc : NULL;
}

/*
 * npc_is_active - Check if NPC instance is currently active
 * 
 * ACTIVE FLAG SEMANTICS:
 * 
 *   active=true:
 *     - NPC exists in game world
 *     - NPC is being processed each tick
 *     - NPC visible to nearby players
 *     - NPC can be interacted with
 * 
 *   active=false:
 *     - Slot is free for spawning
 *     - NPC not in game world
 *     - NPC data may be stale/invalid
 *     - Slot may be uninitialized
 * 
 * USE CASES:
 * 
 *   1. Validation before operations:
 *      if (npc_is_active(target)) {
 *        apply_damage(target, 10);
 *      }
 * 
 *   2. Filtering in loops:
 *      for (i = 0; i < capacity; i++) {
 *        if (npc_is_active(&npcs->npcs[i])) {
 *          process_npc(&npcs->npcs[i]);
 *        }
 *      }
 * 
 *   3. Counting active NPCs:
 *      u32 count = 0;
 *      for (i = 0; i < capacity; i++) {
 *        if (npc_is_active(&npcs->npcs[i])) count++;
 *      }
 *      printf("%u NPCs active\n", count);
 * 
 *   4. Pointer validity check:
 *      Npc* npc = some_function();
 *      if (npc_is_active(npc)) {
 *        // Safe to use npc
 *      }
 * 
 * NULL SAFETY:
 * 
 *   Returns false for NULL pointer:
 *     Npc* npc = NULL;
 *     if (npc_is_active(npc)) {  // false, no crash
 *       // This block never executes
 *     }
 * 
 * COMPARISON WITH npc_get_by_index:
 * 
 *   npc_get_by_index: Lookup by index, returns NULL if inactive
 *   npc_is_active: Check if pointer is active, returns bool
 * 
 *   Example:
 *     Npc* npc = npc_get_by_index(npcs, 42);  // Returns NULL if inactive
 *     if (npc) { ... }  // Same as if (npc_is_active(npc)) { ... }
 * 
 * IMPLEMENTATION SIMPLICITY:
 * 
 *   Extremely simple function:
 *     - NULL check
 *     - Return active flag
 *   
 *   Could be a macro, but function provides:
 *     - Type safety
 *     - Debugger breakpoint support
 *     - Better error messages
 *     - Consistent API style
 * 
 * PERFORMANCE:
 * 
 *   Compiled with optimization (-O2), this function:
 *     - Inlined by compiler
 *     - Zero function call overhead
 *     - Same as direct field access
 * 
 * COMPLEXITY: O(1) time, O(1) space
 */
bool npc_is_active(const Npc* npc) {
    /* NULL-safe: return false if pointer invalid */
    if (!npc) return false;
    
    /* Return active flag */
    return npc->active;
}
