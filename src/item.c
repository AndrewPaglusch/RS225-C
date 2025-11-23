/*******************************************************************************
 * ITEM.C - RuneScape Item System Implementation
 *******************************************************************************
 * 
 * This file implements the RS2 item system, including:
 *   - Item definition management (loaded from cache)
 *   - Runtime item instance tracking
 *   - Generic item container operations
 *   - Stackable item handling
 * 
 * KEY ALGORITHMS IMPLEMENTED:
 *   1. Direct-index item definition lookup (O(1) by item ID)
 *   2. Linear search for stackable item consolidation (O(n) per add)
 *   3. Slot-based item removal (O(1) by slot index)
 *   4. Bulk container clearing with memset (O(n) for n slots)
 * 
 * EDUCATIONAL CONCEPTS:
 *   - Separation of concerns (definition vs instance)
 *   - Memory-efficient data structures
 *   - Cache locality and array-based storage
 *   - Trade-offs between time and space complexity
 * 
 * DATA FLOW:
 * 
 * Server Startup:
 *   main() -> item_system_create() -> item_system_init() -> load cache
 * 
 * Item Usage:
 *   Player picks up item
 *     -> item_container_add(inventory, item_id, amount)
 *       -> item_get_definition() to check stackability
 *       -> Search for existing stack OR find empty slot
 *       -> Update slot
 * 
 *   Player drops item
 *     -> item_container_remove(inventory, slot, amount)
 *       -> Decrease slot amount
 *       -> Clear slot if amount reaches 0
 * 
 * MEMORY LAYOUT OVERVIEW:
 * 
 * ┌──────────────────────────────────────────────────────────────┐
 * │                    GLOBAL ITEM SYSTEM                        │
 * │  g_items -> ItemSystem                                       │
 * │    ├─> definitions: ItemDefinition[10000] (2.5 MB)           │
 * │    ├─> definition_count: 10000                               │
 * │    └─> initialized: true                                     │
 * └──────────────────────────────────────────────────────────────┘
 *                           │
 *        ┌──────────────────┼───────────────────┐
 *        v                  v                   v
 * ┌───────────┐      ┌───────────┐      ┌───────────┐
 * │ Player 1  │      │ Player 2  │      │ Player N  │
 * │ inventory │      │ inventory │      │ inventory │
 * │   (28)    │      │   (28)    │      │   (28)    │
 * └───────────┘      └───────────┘      └───────────┘
 * 
 * Each player has their own ItemContainer instances (inventory, bank, etc.)
 * All players share the same read-only ItemDefinition array.
 * 
 ******************************************************************************/

#include "item.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*******************************************************************************
 * GLOBAL ITEM SYSTEM
 * 
 * g_items is the singleton ItemSystem instance.
 * Initialized once at server startup, accessed globally.
 * 
 * NULL initially, must be created with item_system_create().
 ******************************************************************************/
ItemSystem* g_items = NULL;

/*******************************************************************************
 * ITEM SYSTEM LIFECYCLE FUNCTIONS
 ******************************************************************************/

/*
 * item_system_create - Allocate new ItemSystem structure
 * 
 * @return  Pointer to newly allocated ItemSystem, or NULL on failure
 * 
 * ALGORITHM:
 *   1. Allocate ItemSystem struct with calloc
 *   2. Initialize fields to default values
 *   3. Return pointer
 * 
 * MEMORY ALLOCATION:
 * 
 * calloc() vs malloc():
 *   - calloc(count, size) allocates count * size bytes
 *   - Automatically zero-initializes all bytes
 *   - Safer than malloc (prevents garbage data)
 *   - Slightly slower (due to zeroing overhead)
 * 
 * For ItemSystem:
 *   calloc(1, sizeof(ItemSystem))
 *   = calloc(1, 24) on 64-bit systems
 *   = 24 bytes of zero-initialized memory
 * 
 * Initial state after allocation:
 *   definitions = NULL  (calloc sets pointers to NULL)
 *   definition_count = 0
 *   initialized = false
 * 
 * FAILURE HANDLING:
 * 
 * If calloc fails (out of memory):
 *   - Returns NULL
 *   - No cleanup needed (nothing was allocated)
 *   - Caller must check return value before using
 * 
 * Example usage:
 *   ItemSystem* sys = item_system_create();
 *   if (!sys) {
 *       fprintf(stderr, "Failed to create item system\n");
 *       return EXIT_FAILURE;
 *   }
 * 
 * WHY SEPARATE CREATE AND INIT?
 * 
 * Two-phase construction pattern:
 *   - Create: Allocate structure (fast, always succeeds or fails quickly)
 *   - Init: Load data from disk (slow, may fail due to I/O errors)
 * 
 * Benefits:
 *   - Clearer error handling
 *   - Ability to retry initialization without reallocating
 *   - Consistent pattern across codebase
 * 
 * COMPLEXITY: O(1) time, O(1) space
 */
ItemSystem* item_system_create() {
    /* Allocate structure on heap with zero-initialization */
    ItemSystem* items = calloc(1, sizeof(ItemSystem));
    if (!items) return NULL;  /* Out of memory */
    
    /* 
     * calloc already zeroed all fields, but we explicitly set
     * initialized to false for code clarity (redundant but readable)
     */
    items->initialized = false;
    return items;
}

/*
 * item_system_destroy - Free all memory used by ItemSystem
 * 
 * @param items  ItemSystem to destroy (NULL-safe)
 * 
 * ALGORITHM:
 *   1. Check for NULL (safe to call with NULL pointer)
 *   2. Free definitions array (if allocated)
 *   3. Free ItemSystem struct itself
 * 
 * MEMORY DEALLOCATION ORDER:
 * 
 * Important: Always free inner allocations before outer structure!
 * 
 * Correct order:
 *   1. free(items->definitions)  // Free array first
 *   2. free(items)               // Then free struct
 * 
 * Wrong order would cause memory leak:
 *   1. free(items)               // Struct gone, can't access ->definitions
 *   2. free(items->definitions)  // ERROR: items is already freed!
 * 
 * VISUAL REPRESENTATION:
 * 
 * Before destruction:
 * ┌────────────┐
 * │  items  ───┼──> ┌──────────────────┐
 * └────────────┘    │  ItemSystem      │
 *                   │  definitions  ───┼──> [array of 10000 ItemDefinitions]
 *                   │  count: 10000    │
 *                   │  initialized: T  │
 *                   └──────────────────┘
 * 
 * After free(items->definitions):
 * ┌────────────┐
 * │  items  ───┼──> ┌──────────────────┐
 * └────────────┘    │  ItemSystem      │
 *                   │  definitions  ───┼──> [FREED]
 *                   │  count: 10000    │
 *                   │  initialized: T  │
 *                   └──────────────────┘
 * 
 * After free(items):
 * ┌────────────┐
 * │  items  ───┼──> [FREED]
 * └────────────┘
 * 
 * NULL SAFETY:
 * 
 * This function is NULL-safe. Calling with NULL is a no-op.
 * 
 * Example:
 *   ItemSystem* sys = NULL;
 *   item_system_destroy(sys);  // Safe, does nothing
 * 
 * DOUBLE-FREE PROTECTION:
 * 
 * This function does NOT protect against double-free:
 *   ItemSystem* sys = item_system_create();
 *   item_system_destroy(sys);
 *   item_system_destroy(sys);  // UNDEFINED BEHAVIOR!
 * 
 * Caller must set pointer to NULL after destroying:
 *   item_system_destroy(g_items);
 *   g_items = NULL;  // Prevent accidental reuse
 * 
 * COMPLEXITY: O(1) time
 */
void item_system_destroy(ItemSystem* items) {
    if (!items) return;  /* NULL-safe early exit */
    
    /* Free definitions array if it was allocated */
    if (items->definitions) {
        free(items->definitions);
    }
    
    /* Free the ItemSystem struct itself */
    free(items);
}

/*
 * item_system_init - Initialize item definitions from cache
 * 
 * @param items  ItemSystem to initialize
 * @return       true on success, false on failure
 * 
 * ALGORITHM:
 *   1. Validate input (items != NULL, not already initialized)
 *   2. Allocate definitions array
 *   3. Load item definitions from cache files (TODO)
 *   4. Mark system as initialized
 *   5. Return success status
 * 
 * CURRENT IMPLEMENTATION:
 * 
 * This is a placeholder implementation. It allocates memory and sets up
 * one example item (coins, ID 995) to demonstrate the system works.
 * 
 * TODO: Replace with actual cache loader that reads from RS2 cache files.
 * 
 * CACHE FILE FORMAT (RS2):
 * 
 * RS2 stores item definitions in cache files:
 *   - cache/item_definitions.dat2 (binary data)
 *   - cache/item_definitions.idx (index/offset table)
 * 
 * Binary format (TLV - Type-Length-Value encoding):
 *   Each definition is a series of TLV entries:
 *   
 *   [opcode:u8][payload]
 *   
 *   Common opcodes:
 *     1  = model ID
 *     2  = name (string)
 *     3  = examine text (string)
 *     4  = zoom
 *     5  = rotation X
 *     6  = rotation Y
 *     7  = offset X
 *     11 = stackable (boolean)
 *     12 = value (i32)
 *     16 = members (boolean)
 *     23 = equipment slot
 *     ...
 *     0  = end of definition
 * 
 * FUTURE IMPLEMENTATION SKETCH:
 * 
 *   FILE* dat = fopen("cache/item_definitions.dat2", "rb");
 *   FILE* idx = fopen("cache/item_definitions.idx", "rb");
 *   
 *   for (int id = 0; id < MAX_ITEMS; id++) {
 *       // Read offset from index file
 *       u32 offset = read_u32(idx);
 *       if (offset == 0) continue;  // Item doesn't exist
 *       
 *       // Seek to definition in data file
 *       fseek(dat, offset, SEEK_SET);
 *       
 *       // Parse TLV entries
 *       while (true) {
 *           u8 opcode = read_u8(dat);
 *           if (opcode == 0) break;  // End of definition
 *           
 *           switch (opcode) {
 *               case 2:  // Name
 *                   read_string(dat, definitions[id].name);
 *                   break;
 *               case 3:  // Examine
 *                   read_string(dat, definitions[id].examine);
 *                   break;
 *               case 11:  // Stackable
 *                   definitions[id].stackable = true;
 *                   break;
 *               // ... handle other opcodes ...
 *           }
 *       }
 *   }
 * 
 * ALLOCATION SIZE CALCULATION:
 * 
 * definition_count = 10,000
 * sizeof(ItemDefinition) = 256 bytes (approximately, with padding)
 * Total allocation = 10,000 * 256 = 2,560,000 bytes (2.5 MB)
 * 
 * Is this too much?
 *   - Modern servers: No, 2.5 MB is trivial
 *   - Embedded systems: Maybe, could use on-demand loading
 *   - Trade-off: Memory for speed (O(1) lookup)
 * 
 * IDEMPOTENCE:
 * 
 * This function is idempotent - calling it multiple times is safe.
 * If already initialized, it returns false without modifying anything.
 * 
 * Example:
 *   item_system_init(sys);  // Returns true
 *   item_system_init(sys);  // Returns false (already initialized)
 * 
 * ERROR HANDLING:
 * 
 * Failure modes:
 *   1. items is NULL -> return false
 *   2. Already initialized -> return false
 *   3. Allocation fails -> return false
 * 
 * On failure, the system remains in a consistent state:
 *   - initialized flag stays false
 *   - definitions pointer stays NULL
 *   - Safe to retry initialization
 * 
 * EXAMPLE ITEM SETUP (COINS):
 * 
 * We initialize item ID 995 (coins) as a demonstration:
 * 
 *   ID: 995
 *   Name: "Coins"
 *   Examine: "Lovely money!"
 *   Stackable: true (multiple coins share one slot)
 *   Tradeable: true (can be traded)
 *   Value: 1 (1 coin = 1 gp)
 * 
 * This is the most commonly used item in RS2, so it's a good test case.
 * 
 * COMPLEXITY: O(n) where n = definition_count (due to calloc zeroing)
 */
bool item_system_init(ItemSystem* items) {
    if (!items || items->initialized) return false;
    
    /* 
     * Allocate space for 10,000 item definitions.
     * This covers all RS2 2007 items plus room for future additions.
     * 
     * calloc() zero-initializes all memory, so:
     *   - All item IDs start at 0
     *   - All names are empty strings
     *   - All boolean flags are false
     *   - All numeric fields are 0
     */
    items->definition_count = 10000;
    items->definitions = calloc(items->definition_count, sizeof(ItemDefinition));
    
    if (!items->definitions) {
        /* Allocation failed (out of memory) */
        return false;
    }
    
    /*
     * TODO: Load actual item definitions from cache files
     * 
     * For now, manually initialize one item (coins) as proof-of-concept.
     * This demonstrates the system works without requiring cache files.
     */
    
    /* 
     * Initialize item ID 995: Coins
     * 
     * Direct array access is safe because we just allocated 10,000 slots.
     * 995 < 10000, so this is in bounds.
     */
    ItemDefinition* coins = &items->definitions[995];
    coins->id = 995;
    
    /* 
     * String copy with strcpy
     * 
     * Safe because:
     *   - Source strings are literals (always null-terminated)
     *   - Destination buffers are large enough:
     *     - name[64] can hold "Coins" (5 + 1 null byte)
     *     - examine[128] can hold "Lovely money!" (13 + 1 null byte)
     */
    strcpy(coins->name, "Coins");
    strcpy(coins->examine, "Lovely money!");
    
    /* Set item properties */
    coins->stackable = true;   /* Multiple coins stack in one slot */
    coins->tradeable = true;   /* Can be traded between players */
    coins->value = 1;          /* 1 coin = 1 gold piece */
    
    printf("Initialized item system with %u definitions\n", items->definition_count);
    
    /* Mark system as initialized (enables other functions) */
    items->initialized = true;
    return true;
}

/*
 * item_get_definition - Retrieve item definition by ID
 * 
 * @param items  ItemSystem to query
 * @param id     Item ID (0-65535)
 * @return       Pointer to ItemDefinition, or NULL if not found
 * 
 * ALGORITHM:
 *   1. Validate inputs (items exists, system initialized, id in range)
 *   2. Return pointer to definitions[id]
 * 
 * LOOKUP METHOD: Direct Array Indexing
 * 
 * This is NOT a hash table lookup. It's simple array indexing.
 * 
 * C array indexing:
 *   definitions[id] is equivalent to *(definitions + id)
 * 
 * Pointer arithmetic:
 *   Base address: definitions (e.g., 0x10000000)
 *   Element size: sizeof(ItemDefinition) (e.g., 256 bytes)
 *   Offset calculation: base + (id * element_size)
 *   
 *   Example for id=995:
 *     Address = 0x10000000 + (995 * 256)
 *             = 0x10000000 + 254720
 *             = 0x1003E300
 * 
 * CPU EXECUTION:
 * 
 * Modern CPUs perform this in one instruction:
 *   LEA (Load Effective Address) or similar
 * 
 * Pseudocode:
 *   MOV RAX, [items->definitions]    ; Load base address
 *   MOV RBX, id                      ; Load index
 *   IMUL RBX, 256                    ; Multiply by element size
 *   ADD RAX, RBX                     ; Add offset to base
 *   ; RAX now points to definitions[id]
 * 
 * WHY O(1)?
 * 
 * Array indexing is O(1) because:
 *   - No searching required (we know exact position)
 *   - No iteration (direct calculation)
 *   - No data structure traversal (no linked lists, trees, etc.)
 *   - Just arithmetic: base + (index * size)
 * 
 * COMPARISON: Hash Table vs Direct Indexing
 * 
 * Hash Table Approach:
 *   1. Compute hash: hash = hash_function(id)
 *   2. Find bucket: bucket = hash % table_size
 *   3. Search bucket: for (entry in bucket) if (entry.key == id) return entry
 *   Time: O(1) average, O(n) worst case (collisions)
 *   Space: More compact if IDs are sparse
 * 
 * Direct Indexing Approach (current):
 *   1. Return definitions[id]
 *   Time: O(1) guaranteed (no collisions)
 *   Space: Wastes memory on unused IDs
 * 
 * When to use each:
 *   - Hash table: Sparse, random IDs (e.g., UUID-based)
 *   - Direct index: Dense, sequential IDs (e.g., RS2 item IDs)
 * 
 * BOUNDS CHECKING:
 * 
 * We check if id >= definition_count before accessing.
 * This prevents buffer overflow / out-of-bounds access.
 * 
 * Example with definition_count=10000:
 *   id=995:   995 < 10000  -> OK, return &definitions[995]
 *   id=50000: 50000 >= 10000 -> ERROR, return NULL
 * 
 * What happens without bounds check?
 *   definitions[50000] would access memory beyond the array
 *   - Best case: Read garbage data
 *   - Worst case: Segmentation fault (access violation)
 * 
 * RETURN VALUE USAGE:
 * 
 * This function returns a pointer (not a copy).
 * Caller receives direct access to the ItemDefinition in the array.
 * 
 * Implications:
 *   - Read-only access: Caller should NOT modify the definition
 *   - Fast: No copying of 256-byte struct
 *   - Lifetime: Pointer valid until item_system_destroy() called
 * 
 * Example usage:
 *   ItemDefinition* def = item_get_definition(g_items, 995);
 *   if (def) {
 *       printf("Item: %s\n", def->name);
 *       if (def->stackable) {
 *           printf("This item can stack!\n");
 *       }
 *   } else {
 *       printf("Item not found\n");
 *   }
 * 
 * NULL CHECKS:
 * 
 * Always check return value before dereferencing:
 *   ItemDefinition* def = item_get_definition(g_items, id);
 *   if (!def) return;  // Item doesn't exist
 *   printf("%s\n", def->name);  // Safe to use
 * 
 * THREAD SAFETY:
 * 
 * This function is thread-safe for reading:
 *   - Multiple threads can call simultaneously
 *   - No race conditions (read-only access)
 *   - No locks needed
 * 
 * NOT thread-safe if definitions are being modified:
 *   - Don't call while item_system_init() is running
 *   - Don't modify definitions after initialization
 * 
 * COMPLEXITY: O(1) time, O(1) space
 */
ItemDefinition* item_get_definition(ItemSystem* items, u16 id) {
    /* 
     * Input validation
     * 
     * Check all conditions that would make the lookup invalid:
     *   1. items is NULL (system doesn't exist)
     *   2. initialized is false (definitions not loaded)
     *   3. id >= definition_count (out of bounds)
     */
    if (!items || !items->initialized || id >= items->definition_count) {
        return NULL;
    }
    
    /* 
     * Return pointer to definition at index id
     * 
     * This is safe because:
     *   - We validated id < definition_count (bounds check)
     *   - definitions array is guaranteed to be allocated (initialized check)
     *   - Array was allocated with definition_count elements
     */
    return &items->definitions[id];
}

/*******************************************************************************
 * ITEM CONTAINER MANAGEMENT
 ******************************************************************************/

/*
 * item_container_create - Allocate new item container
 * 
 * @param capacity  Number of slots (e.g., 28 for player inventory)
 * @return          Pointer to new ItemContainer, or NULL on failure
 * 
 * ALGORITHM:
 *   1. Allocate ItemContainer struct
 *   2. Allocate items array (capacity slots)
 *   3. Zero-initialize array (all slots empty)
 *   4. Return pointer
 * 
 * TWO-STEP ALLOCATION:
 * 
 * This function performs two heap allocations:
 * 
 * Allocation 1: ItemContainer struct
 *   Size: sizeof(ItemContainer) = 16 bytes (on 64-bit)
 *   Contains: pointer to items array + capacity field
 * 
 * Allocation 2: Items array
 *   Size: capacity * sizeof(Item)
 *   For 28-slot inventory: 28 * 6 = 168 bytes
 * 
 * Why two allocations?
 *   - Flexibility: Can resize array later without moving struct
 *   - Indirection: Multiple pointers can reference same struct
 *   - Standard pattern: Struct holds metadata, array holds data
 * 
 * MEMORY LAYOUT:
 * 
 * For capacity=28:
 * 
 * Heap:
 * ┌────────────────────────────────────────────────────────┐
 * │  ItemContainer struct (16 bytes)                       │
 * ├────────────────────────────────────────────────────────┤
 * │  items: 0x20000000  (pointer to array)                 │
 * │  capacity: 28                                          │
 * │  (padding: 4 bytes)                                    │
 * └────────────────────────────────────────────────────────┘
 *          │
 *          v
 * ┌───────────────────────────────────────────────────────┐
 * │  Items array at 0x20000000 (168 bytes)                │
 * ├──────┬──────┬──────┬──────┬───────────────┬──────┬────┤
 * │ [0]  │ [1]  │ [2]  │ [3]  │      ...      │ [26] │[27]│
 * ├──────┼──────┼──────┼──────┼───────────────┼──────┼────┤
 * │id: 0 │id: 0 │id: 0 │id: 0 │               │id: 0 │id:0│
 * │amt:0 │amt:0 │amt:0 │amt:0 │               │amt:0 │am:0│
 * └──────┴──────┴──────┴──────┴───────────────┴──────┴────┘
 * 
 * ZERO-INITIALIZATION:
 * 
 * calloc() sets all bytes to 0, which means:
 *   - item[i].id = 0 for all i (empty slot marker)
 *   - item[i].amount = 0 for all i
 * 
 * Why calloc instead of malloc?
 *   - malloc(): Uninitialized memory (garbage values)
 *   - calloc(): Zero-initialized memory (clean state)
 *   - Cost: calloc() is slightly slower (zeroing overhead)
 *   - Benefit: Prevents undefined behavior from reading uninitialized data
 * 
 * FAILURE HANDLING:
 * 
 * This function can fail at two points:
 * 
 * Failure 1: Struct allocation fails
 *   if (!container) return NULL;
 *   -> Nothing to clean up (no allocation succeeded)
 * 
 * Failure 2: Array allocation fails
 *   if (!container->items) {
 *       free(container);  // Clean up partial allocation
 *       return NULL;
 *   }
 *   -> Must free struct before returning
 * 
 * IMPORTANT: Always clean up partial allocations!
 * 
 * Wrong (memory leak):
 *   ItemContainer* container = calloc(1, sizeof(ItemContainer));
 *   container->items = calloc(capacity, sizeof(Item));
 *   if (!container->items) return NULL;  // LEAK: container not freed!
 * 
 * Correct (no leak):
 *   ItemContainer* container = calloc(1, sizeof(ItemContainer));
 *   container->items = calloc(capacity, sizeof(Item));
 *   if (!container->items) {
 *       free(container);  // Clean up before returning
 *       return NULL;
 *   }
 * 
 * COMMON CONTAINER SIZES:
 * 
 * Player inventory: 28 slots (7x4 grid)
 *   Memory: 16 + (28 * 6) = 184 bytes
 * 
 * Player bank: 816 slots (RS2 maximum)
 *   Memory: 16 + (816 * 6) = 4912 bytes (~5 KB)
 * 
 * Equipment: 14 slots (head, cape, amulet, weapon, body, legs, etc.)
 *   Memory: 16 + (14 * 6) = 100 bytes
 * 
 * Shop inventory: 40 slots (typical)
 *   Memory: 16 + (40 * 6) = 256 bytes
 * 
 * USAGE EXAMPLE:
 * 
 *   // Create inventory
 *   ItemContainer* inv = item_container_create(28);
 *   if (!inv) {
 *       fprintf(stderr, "Failed to create inventory\n");
 *       return;
 *   }
 *   
 *   // Use inventory
 *   item_container_add(inv, 995, 1000);  // Add coins
 *   
 *   // Clean up
 *   item_container_destroy(inv);
 * 
 * COMPLEXITY: O(capacity) time (due to calloc zeroing), O(capacity) space
 */
ItemContainer* item_container_create(u32 capacity) {
    /* Allocate container struct */
    ItemContainer* container = calloc(1, sizeof(ItemContainer));
    if (!container) return NULL;  /* Out of memory */
    
    /* Store capacity (used for bounds checking) */
    container->capacity = capacity;
    
    /* Allocate items array */
    container->items = calloc(capacity, sizeof(Item));
    
    if (!container->items) {
        /* Array allocation failed - clean up struct before returning */
        free(container);
        return NULL;
    }
    
    /* Success - return pointer to initialized container */
    return container;
}

/*
 * item_container_destroy - Free all memory used by container
 * 
 * @param container  ItemContainer to destroy (NULL-safe)
 * 
 * ALGORITHM:
 *   1. Check for NULL
 *   2. Free items array
 *   3. Free container struct
 * 
 * DEALLOCATION ORDER:
 * 
 * Must free in reverse order of allocation:
 *   1. Free items array (inner allocation)
 *   2. Free container struct (outer allocation)
 * 
 * Why this order?
 *   - container->items is a pointer stored IN the container struct
 *   - If we free container first, we lose access to container->items
 *   - Result: memory leak (items array is orphaned)
 * 
 * VISUAL:
 * 
 * Before destruction:
 *   container ─> [ItemContainer]
 *                    │
 *                    └─> items ─> [array of Item structs]
 * 
 * After free(container->items):
 *   container ─> [ItemContainer]
 *                    │
 *                    └─> items ─> [FREED]
 * 
 * After free(container):
 *   container ─> [FREED]
 * 
 * NULL SAFETY:
 * 
 * Safe to call with NULL:
 *   ItemContainer* inv = NULL;
 *   item_container_destroy(inv);  // No-op, returns immediately
 * 
 * DOUBLE-FREE PROTECTION:
 * 
 * This function does NOT protect against double-free.
 * Caller must set pointer to NULL after destroying:
 * 
 *   item_container_destroy(inv);
 *   inv = NULL;  // Prevent accidental reuse
 * 
 * COMPLEXITY: O(1) time
 */
void item_container_destroy(ItemContainer* container) {
    if (!container) return;  /* NULL-safe early exit */
    
    /* Free items array (inner allocation) */
    if (container->items) {
        free(container->items);
    }
    
    /* Free container struct (outer allocation) */
    free(container);
}

/*
 * item_container_add - Add item to container (handles stacking)
 * 
 * @param container  Container to modify
 * @param id         Item ID to add
 * @param amount     Quantity to add (must be > 0)
 * @return           true on success, false on failure
 * 
 * ALGORITHM:
 *   1. Validate inputs
 *   2. Get item definition to check if stackable
 *   3. If stackable: Search for existing slot with same ID
 *      a. If found: Increase amount, return true
 *   4. Search for empty slot (id == 0)
 *   5. If found: Set id and amount, return true
 *   6. Otherwise: Return false (container full)
 * 
 * STACKING LOGIC:
 * 
 * Stackable items (coins, runes, arrows, etc.):
 *   - Multiple items share a single slot
 *   - Slot's amount field increases
 *   - Saves inventory space
 * 
 * Non-stackable items (weapons, armor, food, etc.):
 *   - Each item requires its own slot
 *   - Amount is always 1
 *   - Uses more inventory space
 * 
 * EXAMPLE 1: Adding stackable items (coins)
 * 
 * Initial state:
 *   Slot 0: {id=995, amount=1000}  (1000 coins)
 *   Slot 1: {id=0, amount=0}       (empty)
 *   Slot 2: {id=0, amount=0}       (empty)
 * 
 * Action: item_container_add(container, 995, 500)
 * 
 * Step 1: Get definition for item 995
 *   def = item_get_definition(g_items, 995)
 *   def->stackable = true
 * 
 * Step 2: Search for existing slot with id=995
 *   Found at slot 0
 * 
 * Step 3: Increase amount
 *   items[0].amount += 500
 *   items[0].amount = 1500
 * 
 * Final state:
 *   Slot 0: {id=995, amount=1500}  (1500 coins, stacked)
 *   Slot 1: {id=0, amount=0}       (empty)
 *   Slot 2: {id=0, amount=0}       (empty)
 * 
 * EXAMPLE 2: Adding non-stackable items (swords)
 * 
 * Initial state:
 *   Slot 0: {id=1277, amount=1}  (bronze sword)
 *   Slot 1: {id=0, amount=0}     (empty)
 *   Slot 2: {id=0, amount=0}     (empty)
 * 
 * Action: item_container_add(container, 1277, 1)
 * 
 * Step 1: Get definition for item 1277
 *   def = item_get_definition(g_items, 1277)
 *   def->stackable = false
 * 
 * Step 2: Skip stacking search (not stackable)
 * 
 * Step 3: Search for empty slot
 *   Found at slot 1
 * 
 * Step 4: Set id and amount
 *   items[1].id = 1277
 *   items[1].amount = 1
 * 
 * Final state:
 *   Slot 0: {id=1277, amount=1}  (bronze sword)
 *   Slot 1: {id=1277, amount=1}  (bronze sword, separate slot)
 *   Slot 2: {id=0, amount=0}     (empty)
 * 
 * SEARCH ALGORITHM:
 * 
 * Linear search: O(n) where n = capacity
 * 
 * for (u32 i = 0; i < capacity; i++) {
 *     if (condition) {
 *         // Found matching slot
 *         return;
 *     }
 * }
 * 
 * Why not use a faster data structure?
 *   - Capacity is small (28 for inventory, 14 for equipment)
 *   - Linear search of 28 elements is VERY fast (nanoseconds)
 *   - Overhead of hash table would be slower for small n
 * 
 * When would hash table be better?
 *   - Large containers (bank with 816 slots)
 *   - Frequent additions/removals
 *   - Trade-off: complexity for speed
 * 
 * OVERFLOW PROTECTION:
 * 
 * What if amount overflows?
 *   items[i].amount += amount;
 * 
 * Potential issue:
 *   items[i].amount = 4,294,967,295 (max u32)
 *   amount = 1
 *   Result: 4,294,967,296 (overflows to 0)
 * 
 * Current implementation: NO OVERFLOW PROTECTION
 *   - Assumes reasonable amounts
 *   - RS2 client enforces max stack size
 *   - Server trusts client (bad for security!)
 * 
 * Better implementation (future):
 *   u32 new_amount = items[i].amount + amount;
 *   if (new_amount < items[i].amount) {
 *       // Overflow detected
 *       items[i].amount = UINT32_MAX;  // Cap at maximum
 *   } else {
 *       items[i].amount = new_amount;
 *   }
 * 
 * FAILURE MODES:
 * 
 * Returns false when:
 *   1. container is NULL (invalid input)
 *   2. container->items is NULL (corrupted container)
 *   3. amount is 0 (no-op, could be true instead)
 *   4. No empty slot found (container full)
 * 
 * CLIENT NOTIFICATION:
 * 
 * When this function returns false, caller should:
 *   - Send "Your inventory is full" message to player
 *   - Drop item on ground instead
 *   - Prevent item pickup
 * 
 * COMPLEXITY:
 *   Best case: O(1) - stackable item in first slot
 *   Average case: O(n/2) - scan half the slots
 *   Worst case: O(n) - scan all slots (full container)
 */
bool item_container_add(ItemContainer* container, u16 id, u32 amount) {
    /* Validate inputs */
    if (!container || !container->items || amount == 0) return false;
    
    /* 
     * Get item definition to check stackability.
     * 
     * item_get_definition() may return NULL if:
     *   - g_items is not initialized
     *   - id is out of range
     * 
     * We check def != NULL and def->stackable to handle this safely.
     */
    ItemDefinition* def = item_get_definition(g_items, id);
    if (def && def->stackable) {
        /*
         * Item is stackable - search for existing slot with same ID
         * 
         * Linear search through all slots:
         *   - Check if items[i].id == id
         *   - If match found: increase amount and return
         *   - If no match: fall through to "find empty slot" logic
         */
        for (u32 i = 0; i < container->capacity; i++) {
            if (container->items[i].id == id) {
                /* Found existing stack - increase amount */
                container->items[i].amount += amount;
                return true;
            }
        }
    }
    
    /* 
     * Either:
     *   - Item is not stackable, OR
     *   - Item is stackable but doesn't exist yet
     * 
     * Find first empty slot (id == 0) and add item there.
     */
    for (u32 i = 0; i < container->capacity; i++) {
        if (container->items[i].id == 0) {
            /* Found empty slot - add item */
            container->items[i].id = id;
            container->items[i].amount = amount;
            return true;
        }
    }
    
    /* No empty slot found - container is full */
    return false;
}

/*
 * item_container_remove - Remove item from specific slot
 * 
 * @param container  Container to modify
 * @param slot       Slot index (0 to capacity-1)
 * @param amount     Quantity to remove
 * @return           true on success, false on failure
 * 
 * ALGORITHM:
 *   1. Validate inputs (container exists, slot in bounds)
 *   2. Get item at slot
 *   3. Validate item exists and has sufficient amount
 *   4. Decrease amount
 *   5. If amount reaches 0: clear slot (id = 0)
 *   6. Return true
 * 
 * SLOT-BASED REMOVAL:
 * 
 * This function requires caller to know the slot index.
 * It does NOT search by item ID.
 * 
 * Why slot-based?
 *   - Client knows which slot was clicked
 *   - O(1) access (no searching needed)
 *   - Deterministic (no ambiguity with multiple stacks)
 * 
 * Example: Player clicks slot 5 to drop item
 *   -> Send packet: {opcode: DROP_ITEM, slot: 5, amount: 1}
 *   -> Server calls: item_container_remove(inv, 5, 1)
 * 
 * PARTIAL vs FULL REMOVAL:
 * 
 * Partial removal (amount < item.amount):
 *   - Decreases amount
 *   - Slot remains occupied
 *   - Item still visible in inventory
 * 
 * Full removal (amount == item.amount):
 *   - Amount reaches 0
 *   - id set to 0 (marks slot as empty)
 *   - Slot becomes available for new items
 * 
 * EXAMPLE 1: Partial removal (coins)
 * 
 * Initial state:
 *   Slot 3: {id=995, amount=1000}  (1000 coins)
 * 
 * Action: item_container_remove(container, 3, 400)
 * 
 * Step 1: Get item at slot 3
 *   item = &container->items[3]
 * 
 * Step 2: Validate
 *   item->id = 995 (not 0, so slot is occupied)
 *   item->amount = 1000 (>= 400, so enough to remove)
 * 
 * Step 3: Decrease amount
 *   item->amount -= 400
 *   item->amount = 600
 * 
 * Step 4: Check if empty
 *   amount = 600 (not 0, so keep slot)
 * 
 * Final state:
 *   Slot 3: {id=995, amount=600}  (600 coins remaining)
 * 
 * EXAMPLE 2: Full removal (sword)
 * 
 * Initial state:
 *   Slot 5: {id=4151, amount=1}  (abyssal whip)
 * 
 * Action: item_container_remove(container, 5, 1)
 * 
 * Step 1: Get item at slot 5
 *   item = &container->items[5]
 * 
 * Step 2: Validate
 *   item->id = 4151 (not 0, so slot is occupied)
 *   item->amount = 1 (>= 1, so enough to remove)
 * 
 * Step 3: Decrease amount
 *   item->amount -= 1
 *   item->amount = 0
 * 
 * Step 4: Check if empty
 *   amount = 0, so clear slot
 *   item->id = 0
 * 
 * Final state:
 *   Slot 5: {id=0, amount=0}  (empty slot)
 * 
 * BOUNDS CHECKING:
 * 
 * We check slot < capacity to prevent out-of-bounds access.
 * 
 * Example with capacity=28:
 *   slot=5:  5 < 28  -> OK
 *   slot=27: 27 < 28 -> OK (last valid slot)
 *   slot=28: 28 >= 28 -> ERROR, return false
 *   slot=100: 100 >= 28 -> ERROR, return false
 * 
 * What happens without bounds check?
 *   items[100] would access memory beyond array
 *   - Undefined behavior
 *   - Could read/write arbitrary memory
 *   - Security vulnerability (buffer overflow)
 * 
 * INSUFFICIENT AMOUNT:
 * 
 * If item.amount < amount, the removal fails.
 * 
 * Example:
 *   Slot has 50 coins, player tries to drop 100 coins
 *   -> item->amount (50) < amount (100)
 *   -> return false
 * 
 * Why not remove all available?
 *   - Caller expects exact amount
 *   - Partial success is confusing
 *   - Better to fail and let caller decide
 * 
 * EMPTY SLOT:
 * 
 * If slot is empty (id == 0), removal fails.
 * 
 * This prevents:
 *   - Removing from uninitialized memory
 *   - Decrementing amount of non-existent item
 * 
 * CLIENT BUGS:
 * 
 * If client sends invalid slot:
 *   - Bounds check prevents crash
 *   - Function returns false
 *   - Server logs error (optional)
 *   - Player sees no visible change
 * 
 * COMPLEXITY: O(1) time (direct array access)
 */
bool item_container_remove(ItemContainer* container, u32 slot, u32 amount) {
    /* Validate inputs */
    if (!container || !container->items || slot >= container->capacity) return false;
    
    /* 
     * Get pointer to item at specified slot
     * 
     * This is safe because:
     *   - slot < capacity (bounds checked above)
     *   - items array is allocated with capacity elements
     */
    Item* item = &container->items[slot];
    
    /* 
     * Validate item exists and has sufficient amount
     * 
     * id == 0: Slot is empty
     * amount < amount: Not enough items to remove
     */
    if (item->id == 0 || item->amount < amount) return false;
    
    /* 
     * Decrease amount
     * 
     * This is safe because we validated item->amount >= amount above.
     * No risk of underflow (unsigned integer wrapping to large value).
     */
    item->amount -= amount;
    
    /* 
     * Check if slot is now empty
     * 
     * If amount reached 0:
     *   - Clear id (marks slot as empty)
     *   - Allows slot to be reused by item_container_add()
     * 
     * Note: amount is already 0 from subtraction above,
     *       so we only need to clear id
     */
    if (item->amount == 0) {
        item->id = 0;
    }
    
    return true;
}

/*
 * item_container_get - Retrieve item at specific slot
 * 
 * @param container  Container to query
 * @param slot       Slot index (0 to capacity-1)
 * @return           Pointer to Item struct, or NULL if invalid
 * 
 * ALGORITHM:
 *   1. Validate inputs (container exists, slot in bounds)
 *   2. Return pointer to items[slot]
 * 
 * PURPOSE:
 * 
 * This function provides read-only access to a specific slot.
 * Caller can inspect item ID and amount without modifying.
 * 
 * Common use cases:
 *   - Display inventory to player
 *   - Check if player has specific item
 *   - Serialize inventory for saving
 *   - Send inventory state to client
 * 
 * RETURN VALUE:
 * 
 * Returns a POINTER, not a copy.
 * 
 * Implications:
 *   - No copying (fast, O(1))
 *   - Caller can modify item (if they want to)
 *   - Pointer valid until container destroyed
 * 
 * Example usage:
 *   Item* item = item_container_get(inv, 5);
 *   if (item && item->id != 0) {
 *       // Slot 5 is occupied
 *       ItemDefinition* def = item_get_definition(g_items, item->id);
 *       printf("Slot 5: %u x %s\n", item->amount, def->name);
 *   } else {
 *       // Slot 5 is empty or invalid
 *       printf("Slot 5: empty\n");
 *   }
 * 
 * CHECKING FOR EMPTY SLOTS:
 * 
 * This function returns a pointer even if slot is empty.
 * Caller must check item->id to determine if slot is occupied.
 * 
 * Example:
 *   Item* item = item_container_get(inv, 10);
 *   if (!item) {
 *       // Invalid slot index
 *       return;
 *   }
 *   
 *   if (item->id == 0) {
 *       // Slot exists but is empty
 *       printf("Empty slot\n");
 *   } else {
 *       // Slot contains an item
 *       printf("Item ID: %u\n", item->id);
 *   }
 * 
 * BOUNDS CHECKING:
 * 
 * We validate slot < capacity before accessing array.
 * 
 * This prevents:
 *   - Out-of-bounds memory access
 *   - Reading arbitrary memory
 *   - Potential crashes or security issues
 * 
 * MODIFICATION THROUGH POINTER:
 * 
 * Caller receives direct pointer to item in array.
 * They could modify the item:
 * 
 *   Item* item = item_container_get(inv, 5);
 *   item->amount = 999999;  // Cheating!
 * 
 * This is intentional - allows flexibility.
 * Caller is trusted to use responsibly.
 * 
 * If read-only access is needed:
 *   const Item* item = item_container_get(inv, 5);
 *   item->amount = 100;  // Compiler error: const
 * 
 * ITERATION PATTERN:
 * 
 * Common pattern: Iterate through all slots
 * 
 *   for (u32 i = 0; i < container->capacity; i++) {
 *       Item* item = item_container_get(container, i);
 *       if (item && item->id != 0) {
 *           // Process occupied slot
 *           printf("[%u] Item %u x%u\n", i, item->id, item->amount);
 *       }
 *   }
 * 
 * Alternative (direct array access):
 * 
 *   for (u32 i = 0; i < container->capacity; i++) {
 *       Item* item = &container->items[i];
 *       if (item->id != 0) {
 *           printf("[%u] Item %u x%u\n", i, item->id, item->amount);
 *       }
 *   }
 * 
 * Both approaches are equivalent in this implementation.
 * item_container_get() adds bounds checking (safer).
 * 
 * COMPLEXITY: O(1) time
 */
Item* item_container_get(ItemContainer* container, u32 slot) {
    /* Validate inputs */
    if (!container || !container->items || slot >= container->capacity) return NULL;
    
    /* 
     * Return pointer to item at slot
     * 
     * Safe because:
     *   - slot < capacity (bounds checked above)
     *   - items array allocated with capacity elements
     */
    return &container->items[slot];
}

/*
 * item_container_clear - Remove all items from container
 * 
 * @param container  Container to clear
 * 
 * ALGORITHM:
 *   1. Validate input
 *   2. memset entire items array to 0
 * 
 * EFFECT:
 * 
 * Sets all bytes in items array to 0:
 *   - Every slot's id becomes 0 (empty marker)
 *   - Every slot's amount becomes 0
 * 
 * Result: All slots are empty.
 * 
 * MEMSET EXPLAINED:
 * 
 * memset(ptr, value, size)
 *   - ptr: Starting address
 *   - value: Byte to write (0x00 in this case)
 *   - size: Number of bytes to write
 * 
 * For our use:
 *   memset(container->items, 0, container->capacity * sizeof(Item))
 *   
 *   Example with capacity=28:
 *     sizeof(Item) = 6 bytes
 *     size = 28 * 6 = 168 bytes
 *     
 *   Writes 0x00 to bytes [0..167] of items array.
 * 
 * BYTE-LEVEL VIEW:
 * 
 * Before clear (example with 2 slots):
 *   Slot 0: id=995 (0x03E3), amount=1000 (0x000003E8)
 *   Slot 1: id=4151 (0x1037), amount=1 (0x00000001)
 *   
 *   Memory (hex):
 *   [E3 03 E8 03 00 00] [37 10 01 00 00 00]
 *    ^^^^^ id=995       ^^^^^ id=4151
 *          ^^^^^^^^^^^ amount=1000
 *                             ^^^^^^^^^^^ amount=1
 * 
 * After clear:
 *   Memory (hex):
 *   [00 00 00 00 00 00] [00 00 00 00 00 00]
 *    ^^^^^ id=0          ^^^^^ id=0
 *          ^^^^^^^^^^^ amount=0
 *                             ^^^^^^^^^^^ amount=0
 * 
 * WHY MEMSET INSTEAD OF LOOP?
 * 
 * Loop approach:
 *   for (u32 i = 0; i < capacity; i++) {
 *       items[i].id = 0;
 *       items[i].amount = 0;
 *   }
 *   
 *   - Simple to understand
 *   - Portable C code
 *   - Potentially slower (depends on compiler)
 * 
 * memset approach (current):
 *   memset(items, 0, capacity * sizeof(Item))
 *   
 *   - Faster (often uses optimized assembly)
 *   - Single function call
 *   - Less code
 *   - Works because 0 is valid for both id and amount
 * 
 * WHEN IS MEMSET SAFE?
 * 
 * memset to 0 is safe when:
 *   - All fields are integers (no pointers that need special handling)
 *   - 0 is a valid/desired value for all fields
 *   - No padding bits that need special values
 * 
 * For Item struct:
 *   - id: u16, 0 is valid (means empty)
 *   - amount: u32, 0 is valid (empty slot has 0 items)
 *   - No pointers, no special cases
 *   - Safe to memset!
 * 
 * WHEN MEMSET WOULDN'T WORK:
 * 
 * If Item had pointers:
 *   typedef struct {
 *       u16 id;
 *       u32 amount;
 *       char* description;  // Heap-allocated string
 *   } Item;
 *   
 * Then memset would:
 *   - Set description to NULL (good)
 *   - BUT not free existing description (memory leak!)
 * 
 * Would need manual loop:
 *   for (u32 i = 0; i < capacity; i++) {
 *       if (items[i].description) free(items[i].description);
 *       items[i].id = 0;
 *       items[i].amount = 0;
 *       items[i].description = NULL;
 *   }
 * 
 * USE CASES:
 * 
 * Player dies in dangerous area:
 *   - item_container_clear(player->inventory)
 *   - All items dropped on ground
 * 
 * Shop restocking:
 *   - item_container_clear(shop->stock)
 *   - Repopulate with fresh items
 * 
 * Reset temporary container:
 *   - item_container_clear(trade_window)
 *   - Prepare for next trade
 * 
 * PERFORMANCE:
 * 
 * For 28-slot inventory (168 bytes):
 *   - memset takes nanoseconds
 *   - Modern CPUs can write 64+ bytes per cycle
 *   - Effectively O(1) for small containers
 * 
 * For 816-slot bank (4896 bytes):
 *   - Still very fast (microseconds)
 *   - memset is highly optimized in libc
 * 
 * COMPLEXITY: O(capacity) time, O(1) space
 */
void item_container_clear(ItemContainer* container) {
    /* Validate inputs */
    if (!container || !container->items) return;
    
    /* 
     * Zero entire items array
     * 
     * This sets all bytes to 0:
     *   - All id fields become 0 (empty slot marker)
     *   - All amount fields become 0
     * 
     * memset is fast and safe for this use case.
     */
    memset(container->items, 0, container->capacity * sizeof(Item));
}
