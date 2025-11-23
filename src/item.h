/*******************************************************************************
 * ITEM.H - RuneScape Item System Interface
 *******************************************************************************
 * 
 * This header defines the item system architecture for the OSRS server.
 * It provides structures and APIs for:
 *   - Item definition management (loaded from cache)
 *   - Item instance tracking (inventory slots, ground items)
 *   - Container abstraction (inventories, banks, equipment)
 *   - Item stacking mechanics
 * 
 * KEY DATA STRUCTURES:
 *   1. ItemDefinition - Immutable item metadata (stats, stackability, etc.)
 *   2. Item           - Runtime item instance (ID + quantity)
 *   3. ItemContainer  - Dynamic array of item slots (generic container)
 *   4. ItemSystem     - Global item definition database
 * 
 * EDUCATIONAL CONCEPTS:
 *   - Separation of data and state (definition vs instance)
 *   - Hash table lookups by item ID
 *   - Stack-based inventory compression
 *   - Memory-efficient slot representation
 * 
 * IMPLEMENTATION DETAILS:
 *   - Item IDs are 16-bit unsigned integers (0-65535)
 *   - Amounts are 32-bit unsigned integers (0-4,294,967,295)
 *   - Empty slots are represented by id=0, amount=0
 *   - Stackable items consolidate into single slot
 *   - Non-stackable items require one slot per instance
 * 
 ******************************************************************************/

#ifndef ITEM_H
#define ITEM_H

#include "types.h"

/*******************************************************************************
 * ITEM DEFINITION STRUCTURE
 * 
 * ItemDefinition stores immutable metadata loaded from the OSRS cache.
 * This is the "blueprint" for an item - it defines what an item IS,
 * not how many you have or where it's located.
 * 
 * MEMORY LAYOUT: 256 bytes per definition
 * 
 * ┌─────────────────────────────────────────────────────────────┐
 * │  FIELD           │ TYPE  │ SIZE │ OFFSET │ PURPOSE          │
 * ├─────────────────────────────────────────────────────────────┤
 * │  id              │ u16   │  2B  │   0    │ Unique identifier│
 * │  name            │ char[]│ 64B  │   2    │ Display name     │
 * │  examine         │ char[]│ 128B │  66    │ Examine text     │
 * │  sprite_id       │ u16   │  2B  │ 194    │ Inventory icon   │
 * │  value           │ i32   │  4B  │ 196    │ Store price      │
 * │  stackable       │ bool  │  1B  │ 200    │ Can stack?       │
 * │  members         │ bool  │  1B  │ 201    │ Members only?    │
 * │  tradeable       │ bool  │  1B  │ 202    │ Can trade?       │
 * │  noteable        │ bool  │  1B  │ 203    │ Has note form?   │
 * │  note_id         │ u16   │  2B  │ 204    │ Noted item ID    │
 * │  weight          │ i32   │  4B  │ 206    │ Weight (kg*10)   │
 * │  equip_slot      │ u8    │  1B  │ 210    │ Equipment slot   │
 * │  equip_model     │ u16   │  2B  │ 211    │ Worn model ID    │
 * │  bonuses[12]     │ i32[] │ 48B  │ 213    │ Combat bonuses   │
 * │  (padding)       │       │  ?   │ 261    │ (alignment)      │
 * └─────────────────────────────────────────────────────────────┘
 * 
 * EXAMPLE DEFINITIONS:
 * 
 * Coins (ID 995):
 *   name      = "Coins"
 *   examine   = "Lovely money!"
 *   stackable = true
 *   tradeable = true
 *   value     = 1
 * 
 * Abyssal whip (ID 4151):
 *   name      = "Abyssal whip"
 *   examine   = "A weapon from the abyss."
 *   stackable = false
 *   members   = true
 *   tradeable = true
 *   value     = 120001
 *   equip_slot = 3 (weapon)
 *   bonuses = {82, 0, 0, 0, 82, ...} (attack/defense stats)
 * 
 ******************************************************************************/
typedef struct {
    u16  id;              /* Unique item identifier (0-65535) */
    char name[64];        /* Display name (null-terminated) */
    char examine[128];    /* Examine text shown when right-clicking */
    u16  sprite_id;       /* Inventory icon sprite ID */
    i32  value;           /* Store buy/sell price (gold pieces) */
    
    /* Item properties (boolean flags) */
    bool stackable;       /* Can multiple items share one slot? */
    bool members;         /* Members-only item? */
    bool tradeable;       /* Can be traded/sold on GE? */
    bool noteable;        /* Can be converted to noted form? */
    
    u16  note_id;         /* Item ID when noted (0 if not noteable) */
    i32  weight;          /* Weight in kg * 10 (e.g., 125 = 12.5kg) */
    
    /* Equipment properties */
    u8   equip_slot;      /* 0=head, 1=cape, 2=amulet, 3=weapon, etc. */
    u16  equip_model;     /* 3D model ID when equipped */
    
    /* Combat bonuses array:
     * [0] = Stab attack       [6]  = Stab defense
     * [1] = Slash attack      [7]  = Slash defense
     * [2] = Crush attack      [8]  = Crush defense
     * [3] = Magic attack      [9]  = Magic defense
     * [4] = Range attack      [10] = Range defense
     * [5] = Strength bonus    [11] = Prayer bonus
     */
    i32  bonuses[12];
} ItemDefinition;

/*******************************************************************************
 * ITEM INSTANCE STRUCTURE
 * 
 * Item represents a runtime instance of an item - a specific occurrence
 * in a player's inventory, bank, equipment, or on the ground.
 * 
 * MEMORY LAYOUT: 6 bytes per item (8 bytes with padding on 64-bit systems)
 * 
 * ┌──────────────────────────────────────┐
 * │  FIELD   │ TYPE │ SIZE │ PURPOSE     │
 * ├──────────────────────────────────────┤
 * │  id      │ u16  │  2B  │ Item type   │
 * │  amount  │ u32  │  4B  │ Quantity    │
 * └──────────────────────────────────────┘
 * 
 * SPECIAL VALUES:
 * 
 * Empty slot:
 *   id = 0, amount = 0
 * 
 * Single item:
 *   id = 4151 (abyssal whip), amount = 1
 * 
 * Stacked coins:
 *   id = 995 (coins), amount = 1000000
 * 
 * STORAGE OPTIMIZATION:
 * 
 * Traditional approach (storing only present items):
 *   Pros: No wasted memory on empty slots
 *   Cons: Requires linked list or hash table (pointer overhead)
 * 
 * Array approach (fixed-size slot array):
 *   Pros: O(1) access by slot index, cache-friendly, simple
 *   Cons: Wastes 6 bytes per empty slot
 * 
 * For OSRS inventories:
 *   - Inventory size: 28 slots * 6 bytes = 168 bytes total
 *   - Even if half empty: 84 bytes wasted (negligible on modern systems)
 *   - Benefit: Simplicity and predictable memory layout
 * 
 * STACKING BEHAVIOR:
 * 
 * Stackable items (coins, runes, arrows):
 *   - Multiple items share single slot
 *   - Amount field tracks quantity
 *   - Example: 1000 fire runes = 1 slot
 * 
 * Non-stackable items (weapons, armor):
 *   - Each item requires separate slot
 *   - Amount is always 1
 *   - Example: 5 bronze swords = 5 slots
 * 
 * MAXIMUM VALUES:
 * 
 * Item ID range: 0 to 65,535 (2^16 - 1)
 *   - ID 0 reserved for empty slots
 *   - OSRS 2007 has approximately 7,000 items
 *   - Leaves room for future additions
 * 
 * Amount range: 0 to 4,294,967,295 (2^32 - 1)
 *   - Often displayed as "2147M" in-game (signed int32 limit)
 *   - Server can track larger amounts internally
 * 
 ******************************************************************************/
typedef struct {
    u16 id;       /* Item definition ID (0 = empty slot) */
    u32 amount;   /* Quantity (1 for non-stackable items) */
} Item;

/*******************************************************************************
 * ITEM CONTAINER STRUCTURE
 * 
 * ItemContainer is a generic abstraction for any collection of item slots:
 *   - Player inventory (28 slots)
 *   - Bank storage (816 slots in OSRS)
 *   - Equipment (14 slots: head, cape, amulet, weapon, body, shield, etc.)
 *   - Shop inventory (40+ slots)
 *   - Looting bag (28 slots)
 * 
 * MEMORY LAYOUT (for 28-slot inventory):
 * 
 * ┌────────────────────────────────────────────────────────────┐
 * │  ItemContainer struct (16 bytes on 64-bit)                 │
 * ├────────────────────────────────────────────────────────────┤
 * │  items    : Item*  (8B pointer to array)                   │
 * │  capacity : u32    (4B max slots)                          │
 * │  (padding): 4B                                             │
 * └────────────────────────────────────────────────────────────┘
 *                  │
 *                  v
 * ┌────────────────────────────────────────────────────────────┐
 * │  Item array (28 slots * 6 bytes = 168 bytes)               │
 * ├─────────┬─────────┬─────────┬─────────┬──────────┬────────┤
 * │ Slot 0  │ Slot 1  │ Slot 2  │   ...   │ Slot 26  │ Slot 27│
 * ├─────────┼─────────┼─────────┼─────────┼──────────┼────────┤
 * │ id:u16  │ id:u16  │ id:u16  │         │ id:u16   │ id:u16 │
 * │ amt:u32 │ amt:u32 │ amt:u32 │         │ amt:u32  │ amt:u32│
 * └─────────┴─────────┴─────────┴─────────┴──────────┴────────┘
 * 
 * DESIGN RATIONALE:
 * 
 * Why separate capacity field?
 *   - Allows dynamic container sizes without recompiling
 *   - Different players may have different bank sizes (members vs F2P)
 *   - Enables runtime resizing if needed
 * 
 * Why heap-allocated array?
 *   - Stack allocation would limit container to function scope
 *   - Heap allocation allows containers to persist in player structs
 *   - Enables dynamic sizing at runtime
 * 
 * CONTAINER OPERATIONS:
 * 
 * Add item (item_container_add):
 *   1. If stackable and exists: increase amount in existing slot
 *   2. Otherwise: find first empty slot (id == 0)
 *   3. If no empty slots: operation fails
 * 
 * Remove item (item_container_remove):
 *   1. Decrease amount in specified slot
 *   2. If amount reaches 0: clear slot (id = 0)
 * 
 * Get item (item_container_get):
 *   1. Validate slot index
 *   2. Return pointer to Item struct
 * 
 * Clear container (item_container_clear):
 *   1. Memset entire array to 0
 *   2. O(capacity) time - fast for small containers
 * 
 * EXAMPLE USAGE:
 * 
 * Creating a player inventory:
 *   ItemContainer* inv = item_container_create(28);
 * 
 * Adding items:
 *   item_container_add(inv, 995, 10000);  // Add 10k coins
 *   item_container_add(inv, 995, 5000);   // Add 5k more (stacks to 15k)
 *   item_container_add(inv, 4151, 1);     // Add abyssal whip (new slot)
 * 
 * Removing items:
 *   item_container_remove(inv, 0, 5000);  // Remove 5k coins from slot 0
 * 
 * Accessing items:
 *   Item* coins = item_container_get(inv, 0);
 *   printf("Coins: %u\n", coins->amount);
 * 
 ******************************************************************************/
typedef struct {
    Item* items;      /* Heap-allocated array of item slots */
    u32   capacity;   /* Maximum number of slots */
} ItemContainer;

/*******************************************************************************
 * ITEM SYSTEM STRUCTURE
 * 
 * ItemSystem is the global item definition database. It holds all
 * ItemDefinition structs loaded from the OSRS cache on server startup.
 * 
 * MEMORY LAYOUT:
 * 
 * ┌───────────────────────────────────────────────────────────┐
 * │  ItemSystem struct (24 bytes on 64-bit)                   │
 * ├───────────────────────────────────────────────────────────┤
 * │  definitions      : ItemDefinition* (8B pointer)          │
 * │  definition_count : u32 (4B)                              │
 * │  initialized      : bool (1B)                             │
 * │  (padding)        : 3B                                    │
 * └───────────────────────────────────────────────────────────┘
 *                       │
 *                       v
 * ┌───────────────────────────────────────────────────────────┐
 * │  ItemDefinition array (10,000 * 256B = 2.5 MB)            │
 * ├──────────┬──────────┬──────────┬─────┬──────────┬────────┤
 * │  ID 0    │  ID 1    │  ID 2    │ ... │  ID 998  │ ID 999 │
 * │ (empty)  │ (empty)  │ (empty)  │     │ (empty)  │ (empty)│
 * ├──────────┼──────────┼──────────┼─────┼──────────┼────────┤
 * │  ID 995 = Coins                                           │
 * │  name = "Coins", stackable = true, value = 1              │
 * └───────────────────────────────────────────────────────────┘
 * 
 * LOOKUP ALGORITHM:
 * 
 * Item definitions are accessed via direct array indexing (not hashing).
 * This is possible because item IDs are dense (0-10000 range).
 * 
 * Lookup complexity: O(1)
 * 
 * item_get_definition(system, 995):
 *   1. Check if 995 < definition_count (bounds check)
 *   2. Return &definitions[995] (pointer arithmetic)
 * 
 * ALTERNATIVE APPROACHES:
 * 
 * Hash table:
 *   Pros: Can handle sparse ID ranges efficiently
 *   Cons: Hash function overhead, collision handling, pointer chasing
 *   When to use: If item IDs were random (e.g., UUID-based)
 * 
 * Direct indexing (current approach):
 *   Pros: O(1) access, no hashing, cache-friendly
 *   Cons: Wastes memory on unused IDs
 *   When to use: Dense, contiguous ID ranges (like OSRS)
 * 
 * MEMORY OVERHEAD ANALYSIS:
 * 
 * OSRS 2007 has approximately 7,000 items.
 * We allocate 10,000 slots for future-proofing.
 * 
 * Total memory: 10,000 * 256 bytes = 2,560,000 bytes (2.5 MB)
 * Used memory:  7,000 * 256 bytes = 1,792,000 bytes (1.75 MB)
 * Wasted:       3,000 * 256 bytes = 768,000 bytes (0.75 MB)
 * 
 * Is 0.75 MB waste acceptable?
 *   - Modern servers have GBs of RAM
 *   - Simplicity and speed outweigh memory cost
 *   - Trade-off: waste memory to gain performance
 * 
 * INITIALIZATION:
 * 
 * Currently, definitions are initialized to zero.
 * TODO: Load from OSRS cache files (.dat2, .idx)
 * 
 * Cache loading steps (future implementation):
 *   1. Open cache archive (item_definitions.dat2)
 *   2. Read index file to locate definition offsets
 *   3. For each definition:
 *      a. Read binary data (TLV-encoded)
 *      b. Parse fields (name, examine, stats, etc.)
 *      c. Store in definitions[id]
 *   4. Mark system as initialized
 * 
 ******************************************************************************/
typedef struct {
    ItemDefinition* definitions;      /* Array of all item definitions */
    u32             definition_count; /* Number of definitions (array size) */
    bool            initialized;      /* Has system been initialized? */
} ItemSystem;

/*******************************************************************************
 * GLOBAL ITEM SYSTEM INSTANCE
 * 
 * g_items is a global pointer to the server's item system.
 * It is initialized once at server startup and persists until shutdown.
 * 
 * SINGLETON PATTERN:
 * 
 * Why global?
 *   - Item definitions are read-only after loading
 *   - Every function needs access (passing pointer everywhere is verbose)
 *   - No thread-safety concerns (definitions don't change)
 * 
 * Initialization:
 *   main() {
 *       g_items = item_system_create();
 *       item_system_init(g_items);
 *       // ... server main loop ...
 *       item_system_destroy(g_items);
 *   }
 * 
 * Usage throughout codebase:
 *   ItemDefinition* def = item_get_definition(g_items, 995);
 *   if (def->stackable) { ... }
 * 
 * THREAD SAFETY:
 * 
 * Read-only access is thread-safe (no locks needed).
 * All functions that modify item instances work on per-player containers,
 * which are already isolated to their respective threads.
 * 
 ******************************************************************************/
extern ItemSystem* g_items;

/*******************************************************************************
 * ITEM SYSTEM LIFECYCLE FUNCTIONS
 ******************************************************************************/

/*
 * item_system_create - Allocate new ItemSystem (uninitialized)
 * 
 * @return  Pointer to new ItemSystem, or NULL on allocation failure
 * 
 * ALGORITHM:
 *   1. Allocate ItemSystem struct with calloc (zero-initialized)
 *   2. Set initialized = false
 *   3. Return pointer
 * 
 * MEMORY ALLOCATION:
 *   Uses calloc instead of malloc to zero-initialize fields.
 *   This prevents undefined behavior from uninitialized reads.
 * 
 * COMPLEXITY: O(1) time, O(1) space
 */
ItemSystem* item_system_create();

/*
 * item_system_destroy - Free all memory used by ItemSystem
 * 
 * @param items  ItemSystem to destroy (NULL-safe)
 * 
 * ALGORITHM:
 *   1. Check for NULL
 *   2. Free definitions array (if allocated)
 *   3. Free ItemSystem struct
 * 
 * MEMORY DEALLOCATION:
 *   Before:                         After:
 *   ┌──────────┐                    ┌──────────┐
 *   │ g_items ─┼──> [ItemSystem]    │ g_items ─┼──> NULL
 *   └──────────┘         │          └──────────┘
 *                        v
 *              [definitions array]
 * 
 * SAFETY: NULL-safe (can be called with NULL pointer)
 * 
 * COMPLEXITY: O(1) time
 */
void item_system_destroy(ItemSystem* items);

/*
 * item_system_init - Initialize item definitions from cache
 * 
 * @param items  ItemSystem to initialize
 * @return       true on success, false on failure
 * 
 * ALGORITHM:
 *   1. Check if already initialized (idempotent)
 *   2. Allocate definitions array (currently 10,000 slots)
 *   3. Load definitions from cache (TODO: implement cache loader)
 *   4. Set initialized = true
 * 
 * CURRENT IMPLEMENTATION:
 *   - Allocates 10,000 zero-initialized definitions
 *   - Manually initializes item ID 995 (coins) as example
 *   - TODO: Replace with actual cache parser
 * 
 * FUTURE CACHE LOADING:
 *   - Read item_definitions.dat2 from cache/
 *   - Parse binary format (TLV encoding)
 *   - Populate all definitions
 * 
 * ERROR HANDLING:
 *   - Returns false if already initialized
 *   - Returns false if allocation fails
 *   - On failure, system remains uninitialized (safe to retry)
 * 
 * COMPLEXITY: O(n) where n = definition_count
 */
bool item_system_init(ItemSystem* items);

/*
 * item_get_definition - Retrieve item definition by ID
 * 
 * @param items  ItemSystem to query
 * @param id     Item ID (0-65535)
 * @return       Pointer to ItemDefinition, or NULL if not found
 * 
 * ALGORITHM:
 *   1. Validate input (items != NULL, initialized == true)
 *   2. Bounds check: id < definition_count
 *   3. Return &definitions[id]
 * 
 * LOOKUP COMPLEXITY: O(1) time
 * 
 * EXAMPLE:
 *   ItemDefinition* coins = item_get_definition(g_items, 995);
 *   if (coins && coins->stackable) {
 *       printf("%s can stack!\n", coins->name);
 *   }
 * 
 * NULL RETURN:
 *   - items is NULL
 *   - items not initialized
 *   - id >= definition_count
 * 
 * THREAD SAFETY: Read-only, safe to call from any thread
 */
ItemDefinition* item_get_definition(ItemSystem* items, u16 id);

/*******************************************************************************
 * ITEM CONTAINER MANAGEMENT FUNCTIONS
 ******************************************************************************/

/*
 * item_container_create - Allocate new item container with specified capacity
 * 
 * @param capacity  Number of item slots (e.g., 28 for inventory)
 * @return          Pointer to new ItemContainer, or NULL on failure
 * 
 * ALGORITHM:
 *   1. Allocate ItemContainer struct with calloc
 *   2. Allocate items array (capacity * sizeof(Item))
 *   3. Zero-initialize array (empty slots)
 *   4. Return pointer
 * 
 * MEMORY ALLOCATION:
 *   For 28-slot inventory:
 *     ItemContainer struct: 16 bytes
 *     Items array: 28 * 6 = 168 bytes
 *     Total: 184 bytes
 * 
 * FAILURE MODES:
 *   - Returns NULL if struct allocation fails
 *   - Returns NULL if items array allocation fails
 *   - On failure, any partial allocations are cleaned up
 * 
 * COMPLEXITY: O(capacity) time (zero-initialization), O(capacity) space
 */
ItemContainer* item_container_create(u32 capacity);

/*
 * item_container_destroy - Free all memory used by container
 * 
 * @param container  ItemContainer to destroy (NULL-safe)
 * 
 * ALGORITHM:
 *   1. Check for NULL
 *   2. Free items array
 *   3. Free ItemContainer struct
 * 
 * COMPLEXITY: O(1) time
 */
void item_container_destroy(ItemContainer* container);

/*
 * item_container_add - Add item to container (handles stacking)
 * 
 * @param container  Container to modify
 * @param id         Item ID to add
 * @param amount     Quantity to add (must be > 0)
 * @return           true on success, false on failure (container full)
 * 
 * ALGORITHM:
 *   1. Validate inputs (container != NULL, amount > 0)
 *   2. Check if item is stackable via item_get_definition()
 *   3. If stackable:
 *      a. Search for existing slot with matching ID
 *      b. If found: increase amount in that slot, return true
 *   4. Search for empty slot (id == 0)
 *   5. If found: set id and amount, return true
 *   6. Otherwise: return false (container full)
 * 
 * STACKING LOGIC:
 * 
 * Example 1: Adding stackable item (coins)
 *   Initial state:
 *     Slot 0: id=995, amount=1000
 *     Slot 1: id=0, amount=0 (empty)
 *   
 *   item_container_add(container, 995, 500)
 *   
 *   Final state:
 *     Slot 0: id=995, amount=1500 (stacked)
 *     Slot 1: id=0, amount=0 (still empty)
 * 
 * Example 2: Adding non-stackable item (swords)
 *   Initial state:
 *     Slot 0: id=1277, amount=1 (bronze sword)
 *     Slot 1: id=0, amount=0 (empty)
 *   
 *   item_container_add(container, 1277, 1)
 *   
 *   Final state:
 *     Slot 0: id=1277, amount=1 (unchanged)
 *     Slot 1: id=1277, amount=1 (new slot)
 * 
 * SEARCH COMPLEXITY:
 *   - Stackable item exists: O(n) worst case (must scan all slots)
 *   - Non-stackable or new item: O(n) worst case (find empty slot)
 *   - Where n = capacity (e.g., 28 for inventory)
 * 
 * OPTIMIZATION OPPORTUNITY:
 *   Could maintain "first_empty" index to avoid scanning.
 *   Trade-off: faster add, but requires updating on every remove.
 * 
 * RETURN VALUES:
 *   true:  Item successfully added
 *   false: Container full, or invalid input
 */
bool item_container_add(ItemContainer* container, u16 id, u32 amount);

/*
 * item_container_remove - Remove item from specific slot
 * 
 * @param container  Container to modify
 * @param slot       Slot index (0 to capacity-1)
 * @param amount     Quantity to remove
 * @return           true on success, false on failure
 * 
 * ALGORITHM:
 *   1. Validate inputs (container != NULL, slot < capacity)
 *   2. Get item at slot
 *   3. Check if item exists (id != 0) and has enough amount
 *   4. Decrease amount by specified quantity
 *   5. If amount reaches 0: clear slot (id = 0)
 *   6. Return true
 * 
 * PARTIAL REMOVAL EXAMPLE:
 *   Initial: Slot 3 = {id=995, amount=1000}
 *   item_container_remove(container, 3, 400)
 *   Final:   Slot 3 = {id=995, amount=600}
 * 
 * FULL REMOVAL EXAMPLE:
 *   Initial: Slot 5 = {id=4151, amount=1}
 *   item_container_remove(container, 5, 1)
 *   Final:   Slot 5 = {id=0, amount=0} (empty)
 * 
 * FAILURE CASES:
 *   - container is NULL
 *   - slot >= capacity (out of bounds)
 *   - Slot is empty (id == 0)
 *   - Insufficient amount (item.amount < amount)
 * 
 * COMPLEXITY: O(1) time (direct slot access)
 */
bool item_container_remove(ItemContainer* container, u32 slot, u32 amount);

/*
 * item_container_get - Retrieve item at specific slot
 * 
 * @param container  Container to query
 * @param slot       Slot index (0 to capacity-1)
 * @return           Pointer to Item, or NULL if invalid slot
 * 
 * ALGORITHM:
 *   1. Validate inputs
 *   2. Return &items[slot]
 * 
 * USAGE:
 *   Item* item = item_container_get(inv, 5);
 *   if (item && item->id != 0) {
 *       ItemDefinition* def = item_get_definition(g_items, item->id);
 *       printf("Slot 5: %u x %s\n", item->amount, def->name);
 *   }
 * 
 * SAFETY:
 *   - Returns pointer to internal array (caller must not free)
 *   - Pointer is valid until container is destroyed or cleared
 *   - Caller should check if item->id != 0 (slot not empty)
 * 
 * COMPLEXITY: O(1) time
 */
Item* item_container_get(ItemContainer* container, u32 slot);

/*
 * item_container_clear - Remove all items from container
 * 
 * @param container  Container to clear
 * 
 * ALGORITHM:
 *   1. Validate input
 *   2. memset(items, 0, capacity * sizeof(Item))
 * 
 * EFFECT:
 *   All slots become empty (id=0, amount=0)
 * 
 * USE CASES:
 *   - Player dies (clear inventory)
 *   - Shop restocking (clear old items)
 *   - Container reset between uses
 * 
 * COMPLEXITY: O(capacity) time
 */
void item_container_clear(ItemContainer* container);

#endif /* ITEM_H */
