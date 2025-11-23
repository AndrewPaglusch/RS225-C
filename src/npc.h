/*******************************************************************************
 * NPC.H - Non-Player Character System Interface
 *******************************************************************************
 * 
 * This header defines the NPC (Non-Player Character) management system for
 * the RuneScape server emulator. NPCs are computer-controlled entities that
 * populate the game world, including monsters, merchants, quest characters,
 * and ambient entities.
 * 
 * CORE CONCEPTS:
 *   1. NPC Definitions - Immutable templates loaded from game cache
 *   2. NPC Instances - Active, mutable entities in the game world
 *   3. NPC System - Global manager coordinating all NPCs
 *   4. Spawning/Despawning - Entity lifecycle management
 *   5. Update Flags - Change tracking for network protocol
 * 
 * ARCHITECTURE OVERVIEW:
 * 
 *   Game Cache (Disk)
 *        |
 *        v
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                      NPC SYSTEM                             │
 *   ├─────────────────────────────────────────────────────────────┤
 *   │  NpcDefinition Array (Read-Only Templates)                  │
 *   │  ┌────────┬────────┬────────┬─────┬────────┐                │
 *   │  │  Hans  │  Man   │ Goblin │ ... │ Dragon │                │
 *   │  │  id=0  │  id=1  │  id=2  │     │ id=999 │                │
 *   │  └────────┴────────┴────────┴─────┴────────┘                │
 *   │                         ^                                   │
 *   │                         | references                        │
 *   │  Npc Instance Array (Active Entities)                       │
 *   │  ┌────────┬────────┬────────┬─────┬────────┐                │
 *   │  │ npc[0] │ npc[1] │ npc[2] │ ... │npc[N-1]│                │
 *   │  │ active │ active │inactive│     │ active │                │
 *   │  └────────┴────────┴────────┴─────┴────────┘                │
 *   └─────────────────────────────────────────────────────────────┘
 *                         |
 *                         v
 *                  Game World (Players see NPCs)
 * 
 * KEY ALGORITHMS:
 *   - Linear search for free NPC slots (O(N) spawn)
 *   - Direct indexing for NPC lookups (O(1) access)
 *   - Flag-based update tracking (O(1) per flag)
 * 
 * MEMORY LAYOUT EXAMPLE:
 * 
 * For a system with 5000 NPC capacity:
 *   NpcSystem struct:     ~40 bytes
 *   NpcDefinition array:  10000 * 128 bytes = 1.28 MB
 *   Npc instance array:   5000 * 80 bytes = 400 KB
 *   Total:                ~1.7 MB
 * 
 ******************************************************************************/

#ifndef NPC_H
#define NPC_H

#include "types.h"      /* u8, u16, u32, u64, bool */
#include "position.h"   /* Position struct (x, z, height) */
#include "movement.h"   /* MovementHandler (waypoint queue) */

/*******************************************************************************
 * NPC DEFINITION - IMMUTABLE TEMPLATE
 *******************************************************************************
 * 
 * NpcDefinition stores the TEMPLATE data for an NPC type, loaded from the
 * game cache at server startup. This is READ-ONLY configuration data that
 * defines what an NPC "is" (its name, stats, appearance).
 * 
 * SEPARATION OF CONCERNS:
 *   Definition (this struct) = WHAT the NPC is (e.g., "a goblin")
 *   Instance (Npc struct)    = WHERE/WHEN it exists (e.g., "at coords 3200,3200")
 * 
 * EXAMPLE: NPC ID 1 = "Man"
 *   - name: "Man"
 *   - combat_level: 2
 *   - max_hitpoints: 7
 *   - walk_radius: 5 tiles
 *   - aggressive: false
 * 
 * WHY SEPARATE DEFINITIONS FROM INSTANCES?
 *   Memory efficiency: 1 definition, N instances
 *   Example: 1000 "Man" NPCs share 1 definition (128 bytes)
 *            vs. storing name/stats in each instance (128 KB vs. 128 bytes)
 * 
 * STRUCTURE SIZE: 128 bytes (approximate)
 * ALIGNMENT: Natural alignment (largest field is u16 array)
 * 
 ******************************************************************************/
typedef struct {
    /*--------------------------------------------------------------------------
     * IDENTITY
     *--------------------------------------------------------------------------*/
    
    /* Unique identifier for this NPC type (e.g., 0=Hans, 1=Man, 2=Goblin) */
    u16 id;
    
    /* Display name shown to players (max 63 chars + null terminator) */
    char name[64];
    
    /* Right-click examine text (max 127 chars + null terminator) */
    char examine[128];
    
    /*--------------------------------------------------------------------------
     * COMBAT STATS
     *--------------------------------------------------------------------------*/
    
    /* Combat level displayed above NPC (0-255, typically 0-999 in RS) */
    u8 combat_level;
    
    /* Maximum hitpoints (health) for this NPC type */
    u16 max_hitpoints;
    
    /* Attack speed in game ticks (1 tick = 600ms typically)
     * Example: attack_speed=4 means NPC attacks every 2.4 seconds */
    u16 attack_speed;
    
    /* Respawn time in game ticks after death
     * Example: respawn_time=25 means 15 second respawn (25 * 600ms) */
    u16 respawn_time;
    
    /*--------------------------------------------------------------------------
     * MOVEMENT & BEHAVIOR
     *--------------------------------------------------------------------------*/
    
    /* Random walk radius from spawn point (in tiles)
     * Example: walk_radius=5 means NPC can wander 5 tiles away from spawn
     *          walk_radius=0 means NPC never moves */
    u16 walk_radius;
    
    /* If true, NPC will attack nearby players unprovoked */
    bool aggressive;
    
    /* If true, NPC will flee when health drops below threshold */
    bool retreats;
    
    /*--------------------------------------------------------------------------
     * APPEARANCE (RENDERING DATA)
     *--------------------------------------------------------------------------*/
    
    /* 3D model IDs for body parts (head, torso, legs, etc.)
     * Used by client to render NPC
     * Array size 12 accommodates complex models with many parts */
    u16 models[12];
    
    /* Texture color replacements (RGB values)
     * Allows recoloring base model for variety
     * Example: Same goblin model, different skin colors */
    u16 colors[5];
    
    /* Animation ID for standing idle */
    u16 stand_anim;
    
    /* Animation ID for walking */
    u16 walk_anim;
    
    /* Size in tiles (1=1x1, 2=2x2, etc.)
     * Large NPCs like dragons can be 3x3 or larger
     * Affects collision detection and pathfinding */
    u8 size;
    
} NpcDefinition;

/*******************************************************************************
 * NPC INSTANCE - MUTABLE ENTITY
 *******************************************************************************
 * 
 * Npc represents a SPECIFIC instance of an NPC in the game world. Multiple
 * instances can share the same NpcDefinition (e.g., 100 goblins, all using
 * NpcDefinition ID 2).
 * 
 * LIFECYCLE:
 *   1. Inactive (active=false) - Slot is available for spawning
 *   2. Active (active=true) - NPC exists in world, updating each tick
 *   3. Dead (respawn_timer>0) - NPC died, waiting to respawn
 *   4. Despawned (active=false) - Returned to inactive state
 * 
 * STATE DIAGRAM:
 * 
 *   ┌─────────────────┐
 *   │    INACTIVE     │ <──────────────┐
 *   │  active=false   │                │
 *   └────────┬────────┘                │
 *            │ npc_spawn()             │
 *            v                         │
 *   ┌─────────────────┐                │
 *   │     ALIVE       │                │
 *   │   active=true   │                │
 *   │ respawn_timer=0 │                │
 *   └────────┬────────┘                │
 *            │ take damage             │
 *            v                         │
 *   ┌─────────────────┐                │
 *   │      DEAD       │                │
 *   │   active=true   │                │
 *   │ respawn_timer>0 │                │
 *   └────────┬────────┘                │
 *            │ timer reaches 0         │
 *            v                         │
 *   ┌─────────────────┐                │
 *   │    RESPAWN      │                │
 *   │ reset HP, pos   │ ───────────────┘
 *   └─────────────────┘  OR npc_despawn()
 * 
 * STRUCTURE SIZE: ~80 bytes (approximate)
 * ALIGNMENT: Natural (u64 field requires 8-byte alignment)
 * 
 ******************************************************************************/
typedef struct {
    /*--------------------------------------------------------------------------
     * IDENTIFICATION
     *--------------------------------------------------------------------------*/
    
    /* Unique index in global NPC array (0 to npc_capacity-1)
     * Used for network protocol (client identifies NPCs by index) */
    u16 index;
    
    /* Reference to NpcDefinition (template)
     * Use npc_get_definition(npcs, npc->npc_id) to access template */
    u16 npc_id;
    
    /*--------------------------------------------------------------------------
     * SPATIAL STATE
     *--------------------------------------------------------------------------*/
    
    /* Current position in world coordinates
     * Position format: (x, z, height)
     *   x, z: Tile coordinates (1 unit = 1 tile)
     *   height: Vertical layer (0-3 typically) */
    Position position;
    
    /* Original spawn position (for respawning and wander radius)
     * NPC returns here after death or when wander timer expires */
    Position spawn_position;
    
    /* Movement waypoint queue and pathfinding state
     * See movement.h for details */
    MovementHandler movement;
    
    /*--------------------------------------------------------------------------
     * COMBAT STATE
     *--------------------------------------------------------------------------*/
    
    /* Current hitpoints (0 = dead)
     * Decreases when damaged, capped at NpcDefinition.max_hitpoints */
    u16 hitpoints;
    
    /*--------------------------------------------------------------------------
     * NETWORK SYNCHRONIZATION
     *--------------------------------------------------------------------------*/
    
    /* Bitfield of pending visual updates (see UPDATE FLAG CONSTANTS below)
     * Set flags when NPC state changes (moved, attacked, etc.)
     * Client reads flags to update display
     * 
     * FLAG SYSTEM EXPLAINED:
     * 
     * Each bit represents a different type of update:
     *   Bit 0 (0x0001): Animation changed
     *   Bit 1 (0x0002): Force chat (overhead text)
     *   Bit 2 (0x0004): Hit splat (damage indicator)
     *   Bit 3 (0x0008): Appearance changed
     *   ...
     * 
     * Example:
     *   NPC takes damage -> set bit 2: update_flags |= 0x0004
     *   NPC attacks -> set bit 0: update_flags |= 0x0001
     *   After sending update -> clear flags: update_flags = 0
     * 
     * WHY BITFIELDS?
     *   - Compact: 32 flags in 4 bytes vs. 32 bytes for bool array
     *   - Fast: Bitwise OR to set, bitwise AND to check
     *   - Network-efficient: Send 4 bytes + flagged data instead of
     *     entire NPC state every tick
     */
    u32 update_flags;
    
    /*--------------------------------------------------------------------------
     * LIFECYCLE STATE
     *--------------------------------------------------------------------------*/
    
    /* true = NPC exists in world, false = slot available for spawn */
    bool active;
    
    /* Game ticks until respawn (0 = alive or inactive)
     * Set to NpcDefinition.respawn_time when NPC dies
     * Decremented each tick until 0, then NPC respawns */
    u64 respawn_timer;
    
} Npc;

/*******************************************************************************
 * UPDATE FLAG CONSTANTS
 *******************************************************************************
 * 
 * These constants define individual bits in Npc.update_flags. Set these
 * flags when the corresponding NPC state changes.
 * 
 * USAGE EXAMPLE:
 *   NPC attacked, play attack animation:
 *     npc->update_flags |= NPC_UPDATE_ANIMATION;
 *     npc->animation_id = 422;
 *     npc->animation_delay = 0;
 * 
 *   NPC takes damage:
 *     npc->update_flags |= NPC_UPDATE_HIT;
 *     npc->hit_damage = 5;
 *     npc->hit_type = HIT_NORMAL;
 * 
 *   Multiple updates in one tick:
 *     npc->update_flags |= (NPC_UPDATE_ANIMATION | NPC_UPDATE_HIT);
 * 
 * BIT POSITIONS:
 *   These match the RuneScape network protocol specification
 * 
 ******************************************************************************/

/* NPC played an animation (attack, death, emote, etc.) */
#define NPC_UPDATE_ANIMATION    0x0001

/* NPC force chat (overhead text, like quest dialogue) */
#define NPC_UPDATE_FORCE_CHAT   0x0002

/* NPC took damage (show hit splat) */
#define NPC_UPDATE_HIT          0x0004

/* NPC appearance changed (transformation, equipment change) */
#define NPC_UPDATE_APPEARANCE   0x0008

/* NPC face direction changed (turn to face player/target) */
#define NPC_UPDATE_FACE_DIR     0x0010

/* NPC face entity changed (lock facing to player/NPC) */
#define NPC_UPDATE_FACE_ENTITY  0x0020

/*******************************************************************************
 * NPC SYSTEM - GLOBAL MANAGER
 *******************************************************************************
 * 
 * NpcSystem is the central coordinator for all NPC-related data and operations.
 * This is a SINGLETON - only one instance exists per server (g_npcs global).
 * 
 * RESPONSIBILITIES:
 *   1. Store all NPC definitions (loaded from cache)
 *   2. Manage NPC instance pool (fixed-size array)
 *   3. Track active NPC count and capacity
 *   4. Coordinate spawning/despawning
 * 
 * MEMORY MANAGEMENT:
 *   All data is heap-allocated to support configurable capacity
 *   Single allocation per array (cache-friendly, contiguous memory)
 * 
 * INITIALIZATION SEQUENCE:
 *   1. npc_system_create(capacity) - Allocate memory
 *   2. npc_system_init(npcs) - Load definitions from cache
 *   3. Server spawns initial NPCs
 *   4. Server runs game loop (update all active NPCs)
 *   5. npc_system_destroy(npcs) - Cleanup on shutdown
 * 
 * SCALABILITY:
 *   Capacity is configurable (typical: 5000-10000 NPCs)
 *   Memory usage = (definitions * 128 bytes) + (capacity * 80 bytes)
 *   Example: 10000 defs + 5000 capacity = 1.28 MB + 400 KB = 1.68 MB
 * 
 ******************************************************************************/
typedef struct {
    /*--------------------------------------------------------------------------
     * DEFINITION DATABASE (READ-ONLY TEMPLATES)
     *--------------------------------------------------------------------------*/
    
    /* Array of all NPC definitions loaded from game cache
     * Index = NPC ID (e.g., definitions[1] = "Man" template)
     * 
     * MEMORY LAYOUT:
     * ┌──────────┬──────────┬──────────┬─────┬──────────┐
     * │  def[0]  │  def[1]  │  def[2]  │ ... │def[N-1]  │
     * │   Hans   │   Man    │  Goblin  │     │  Dragon  │
     * └──────────┴──────────┴──────────┴─────┴──────────┘
     *  128 bytes  128 bytes  128 bytes        128 bytes
     */
    NpcDefinition* definitions;
    
    /* Number of definitions loaded (max NPC ID + 1) */
    u32 definition_count;
    
    /*--------------------------------------------------------------------------
     * INSTANCE POOL (ACTIVE ENTITIES)
     *--------------------------------------------------------------------------*/
    
    /* Array of NPC instances (active and inactive)
     * Index = NPC's network index (what clients use to track NPCs)
     * 
     * SLOT ALLOCATION:
     *   Active NPCs: npc[i].active = true
     *   Free slots:  npc[i].active = false
     * 
     * MEMORY LAYOUT:
     * ┌──────────┬──────────┬──────────┬─────┬──────────┐
     * │  npc[0]  │  npc[1]  │  npc[2]  │ ... │npc[N-1]  │
     * │ active=1 │ active=1 │ active=0 │     │ active=1 │
     * │  Hans    │   Man    │  (free)  │     │  Goblin  │
     * └──────────┴──────────┴──────────┴─────┴──────────┘
     *  80 bytes   80 bytes   80 bytes         80 bytes
     * 
     * SPAWNING ALGORITHM:
     *   Linear search for first inactive slot:
     *     for (i = 0; i < capacity; i++)
     *       if (!npcs[i].active) return &npcs[i];
     *   Time complexity: O(N) worst case
     * 
     * OPTIMIZATION OPPORTUNITY:
     *   Maintain free list for O(1) allocation
     *   Trade-off: 4 bytes per NPC for next pointer
     */
    Npc* npcs;
    
    /* Maximum number of concurrent NPCs (array size) */
    u32 npc_capacity;
    
    /*--------------------------------------------------------------------------
     * SYSTEM STATE
     *--------------------------------------------------------------------------*/
    
    /* true if npc_system_init() completed successfully
     * Prevents double-initialization and usage before initialization */
    bool initialized;
    
} NpcSystem;

/*******************************************************************************
 * GLOBAL NPC SYSTEM INSTANCE
 *******************************************************************************
 * 
 * SINGLETON PATTERN:
 *   Only one NPC system exists per server process
 *   Declared 'extern' here, defined in npc.c
 * 
 * USAGE:
 *   if (!g_npcs) {
 *     g_npcs = npc_system_create(5000);
 *     npc_system_init(g_npcs);
 *   }
 *   Npc* hans = npc_spawn(g_npcs, 0, 3200, 3200, 0);
 * 
 * WHY GLOBAL?
 *   - Simplifies API (no need to pass npcs to every function)
 *   - Matches game architecture (one world, one NPC system)
 *   - Thread-safe if only modified on main thread
 * 
 ******************************************************************************/
extern NpcSystem* g_npcs;

/*******************************************************************************
 * NPC SYSTEM LIFECYCLE FUNCTIONS
 ******************************************************************************/

/*
 * npc_system_create - Allocate and initialize NPC system
 * 
 * @param capacity  Maximum number of concurrent NPC instances
 * @return          Pointer to NpcSystem, or NULL on allocation failure
 * 
 * ALGORITHM:
 *   1. Allocate NpcSystem struct on heap
 *   2. Allocate NPC instance array (capacity * sizeof(Npc))
 *   3. Zero-initialize all memory (calloc)
 *   4. Set initialized=false (requires npc_system_init)
 * 
 * MEMORY ALLOCATION:
 *   Total heap usage = sizeof(NpcSystem) + (capacity * sizeof(Npc))
 *                   = 40 + (capacity * 80) bytes
 *   Example: capacity=5000 -> 40 + 400000 = 400040 bytes (~400 KB)
 * 
 * ERROR HANDLING:
 *   Returns NULL if malloc/calloc fails (out of memory)
 *   Caller must check return value before use
 * 
 * USAGE:
 *   NpcSystem* npcs = npc_system_create(5000);
 *   if (!npcs) {
 *     fprintf(stderr, "Failed to create NPC system\n");
 *     exit(1);
 *   }
 * 
 * COMPLEXITY: O(capacity) time (zero-initialization), O(capacity) space
 */
NpcSystem* npc_system_create(u32 capacity);

/*
 * npc_system_destroy - Free all memory used by NPC system
 * 
 * @param npcs  NPC system to destroy (may be NULL)
 * 
 * ALGORITHM:
 *   1. Check for NULL (safe to call on NULL pointer)
 *   2. Free definitions array if allocated
 *   3. Free NPC instances array if allocated
 *   4. Free NpcSystem struct
 * 
 * CLEANUP ORDER:
 *   Must free child allocations before parent:
 *     1. npcs->definitions (child)
 *     2. npcs->npcs (child)
 *     3. npcs (parent)
 * 
 * USAGE:
 *   npc_system_destroy(g_npcs);
 *   g_npcs = NULL;  // Prevent use-after-free
 * 
 * SAFETY:
 *   - NULL-safe (can call with NULL pointer)
 *   - Does NOT free MovementHandler internal allocations
 *     (must call npc_despawn() on all active NPCs first)
 * 
 * COMPLEXITY: O(1) time
 */
void npc_system_destroy(NpcSystem* npcs);

/*
 * npc_system_init - Load NPC definitions from game cache
 * 
 * @param npcs  NPC system to initialize
 * @return      true on success, false on failure
 * 
 * ALGORITHM:
 *   1. Check if already initialized (prevent double-init)
 *   2. Allocate definitions array
 *   3. Load NPC data from cache files (TODO: implement cache reading)
 *   4. Currently: hardcoded sample definitions (Hans, Man)
 *   5. Set initialized=true
 * 
 * DEFINITION LOADING (FUTURE):
 *   Read from cache files (typically .dat and .idx files):
 *     for each NPC ID in cache:
 *       Parse binary definition data
 *       Decode model IDs, colors, animations
 *       Store in definitions array
 * 
 * HARDCODED DEFINITIONS (CURRENT):
 *   ID 0: Hans (Lumbridge Castle servant, non-combat)
 *   ID 1: Man (Lumbridge resident, level 2)
 * 
 * ERROR HANDLING:
 *   Returns false if:
 *     - npcs is NULL
 *     - Already initialized
 *     - Memory allocation fails
 * 
 * USAGE:
 *   if (!npc_system_init(g_npcs)) {
 *     fprintf(stderr, "Failed to initialize NPC system\n");
 *     npc_system_destroy(g_npcs);
 *     exit(1);
 *   }
 * 
 * COMPLEXITY: O(definition_count) time and space
 */
bool npc_system_init(NpcSystem* npcs);

/*
 * npc_get_definition - Retrieve NPC template by ID
 * 
 * @param npcs  NPC system
 * @param id    NPC definition ID (0 to definition_count-1)
 * @return      Pointer to NpcDefinition, or NULL if invalid ID
 * 
 * BOUNDS CHECKING:
 *   Returns NULL if:
 *     - npcs is NULL
 *     - System not initialized
 *     - id >= definition_count (out of range)
 * 
 * USAGE:
 *   NpcDefinition* def = npc_get_definition(g_npcs, npc->npc_id);
 *   if (def) {
 *     printf("NPC name: %s\n", def->name);
 *     printf("Combat level: %u\n", def->combat_level);
 *   }
 * 
 * RETURN VALUE:
 *   Pointer to definition (DO NOT FREE - managed by system)
 *   Valid until npc_system_destroy() is called
 * 
 * COMPLEXITY: O(1) time
 */
NpcDefinition* npc_get_definition(NpcSystem* npcs, u16 id);

/*******************************************************************************
 * NPC INSTANCE MANAGEMENT FUNCTIONS
 ******************************************************************************/

/*
 * npc_spawn - Create new NPC instance at specified location
 * 
 * @param npcs    NPC system
 * @param npc_id  NPC definition ID (template to spawn)
 * @param x       World X coordinate (tile units)
 * @param z       World Z coordinate (tile units)
 * @param height  Height layer (0-3 typically)
 * @return        Pointer to spawned NPC, or NULL on failure
 * 
 * ALGORITHM:
 *   1. Find first inactive NPC slot (linear search)
 *   2. If no slots available, return NULL
 *   3. Initialize NPC state:
 *      - Set npc_id (reference to definition)
 *      - Set current position and spawn position
 *      - Initialize movement handler
 *      - Set hitpoints from definition
 *      - Clear update flags
 *      - Set active=true
 *      - Reset respawn_timer=0
 *   4. Return pointer to NPC
 * 
 * SLOT ALLOCATION:
 *   Linear search for free slot:
 *     Time complexity: O(N) worst case (all slots full)
 *     Space complexity: O(1)
 * 
 *   Example with capacity=5:
 *   ┌────────┬────────┬────────┬────────┬────────┐
 *   │ npc[0] │ npc[1] │ npc[2] │ npc[3] │ npc[4] │
 *   │active=1│active=1│active=0│active=1│active=1│
 *   └────────┴────────┴────────┴────────┴────────┘
 *                         ↑
 *                     allocated here
 * 
 * INITIALIZATION:
 *   Current position = spawn position initially
 *   NPC can wander within walk_radius of spawn position
 *   On death, NPC respawns at spawn_position
 * 
 * FAILURE CASES:
 *   Returns NULL if:
 *     - npcs is NULL
 *     - System not initialized
 *     - All NPC slots are in use (capacity reached)
 * 
 * USAGE:
 *   Spawn Hans at Lumbridge Castle:
 *     Npc* hans = npc_spawn(g_npcs, 0, 3222, 3218, 0);
 *     if (!hans) {
 *       fprintf(stderr, "Failed to spawn NPC (capacity full)\n");
 *     }
 * 
 * NETWORK IMPLICATIONS:
 *   Client will be notified of spawn on next update tick
 *   NPC appears in players' viewports if within range
 * 
 * COMPLEXITY: O(N) time (slot search), O(1) space
 */
Npc* npc_spawn(NpcSystem* npcs, u16 npc_id, u32 x, u32 z, u32 height);

/*
 * npc_despawn - Remove NPC from world and free slot
 * 
 * @param npcs  NPC system
 * @param npc   NPC instance to despawn
 * 
 * ALGORITHM:
 *   1. Validate parameters (NULL checks, active check)
 *   2. Set active=false (mark slot as available)
 *   3. Cleanup movement handler (free waypoint queue)
 *   4. NPC state remains in memory (slot reused on next spawn)
 * 
 * CLEANUP:
 *   Frees MovementHandler internal allocations (waypoint queue)
 *   Does NOT free NPC struct itself (part of array)
 * 
 * USAGE:
 *   Quest complete, remove quest NPC:
 *     npc_despawn(g_npcs, quest_npc);
 *     quest_npc = NULL;  // Prevent use-after-despawn
 * 
 * NETWORK IMPLICATIONS:
 *   Client will be notified of despawn on next update tick
 *   NPC disappears from players' viewports
 * 
 * SAFETY:
 *   Safe to call multiple times (checks active flag)
 *   Safe to call with NULL npc pointer
 * 
 * COMPLEXITY: O(1) time (assumes movement_destroy is O(1))
 */
void npc_despawn(NpcSystem* npcs, Npc* npc);

/*
 * npc_process - Update NPC state for one game tick
 * 
 * @param npc  NPC instance to update
 * 
 * ALGORITHM:
 *   1. Validate NPC is active
 *   2. Process movement (move to next waypoint if walking)
 *   3. TODO: Process combat AI
 *   4. TODO: Process random walking (if idle)
 *   5. TODO: Process aggression (auto-attack nearby players)
 *   6. TODO: Decrement respawn timer (if dead)
 * 
 * MOVEMENT PROCESSING:
 *   If NPC has waypoints queued:
 *     1. Get next waypoint
 *     2. Move NPC to that position
 *     3. Remove waypoint from queue
 *     4. Set movement update flag for clients
 * 
 * GAME LOOP INTEGRATION:
 *   Called once per tick for each active NPC:
 *     for (i = 0; i < npcs->npc_capacity; i++) {
 *       if (npcs->npcs[i].active) {
 *         npc_process(&npcs->npcs[i]);
 *       }
 *     }
 * 
 * TICK RATE:
 *   RuneScape tick = 600ms (0.6 seconds)
 *   npc_process() called ~1.67 times per second
 * 
 * FUTURE ENHANCEMENTS:
 *   - Combat AI (attack, special attacks, prayers)
 *   - Pathfinding (navigate around obstacles)
 *   - Random walking (wander within walk_radius)
 *   - Aggression detection (auto-attack players)
 *   - Respawn logic (reset HP, teleport to spawn)
 * 
 * COMPLEXITY: O(1) time currently (TODO: depends on AI complexity)
 */
void npc_process(Npc* npc);

/*
 * npc_get_by_index - Retrieve NPC instance by network index
 * 
 * @param npcs   NPC system
 * @param index  NPC index (0 to npc_capacity-1)
 * @return       Pointer to NPC if active, or NULL if inactive/invalid
 * 
 * BOUNDS CHECKING:
 *   Returns NULL if:
 *     - npcs is NULL
 *     - index >= npc_capacity (out of range)
 *     - NPC at index is inactive
 * 
 * USAGE:
 *   Client clicked on NPC index 42:
 *     Npc* target = npc_get_by_index(g_npcs, 42);
 *     if (target) {
 *       player_attack_npc(player, target);
 *     }
 * 
 * NETWORK PROTOCOL:
 *   Client identifies NPCs by index (not by NPC ID)
 *   Index is stable as long as NPC remains active
 *   Index can be reused after despawn
 * 
 * COMPLEXITY: O(1) time
 */
Npc* npc_get_by_index(NpcSystem* npcs, u16 index);

/*
 * npc_is_active - Check if NPC instance is currently active
 * 
 * @param npc  NPC instance to check
 * @return     true if active, false if inactive or NULL
 * 
 * USAGE:
 *   if (npc_is_active(target_npc)) {
 *     apply_damage(target_npc, 5);
 *   }
 * 
 * SAFETY: NULL-safe (returns false for NULL pointer)
 * 
 * COMPLEXITY: O(1) time
 */
bool npc_is_active(const Npc* npc);

#endif /* NPC_H */
