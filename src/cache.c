/*******************************************************************************
 * CACHE.C - RuneScape Cache System Implementation
 *******************************************************************************
 *
 * This file implements the game's asset cache system for efficient loading
 * and retrieval of game resources. The cache provides fast access to
 * compressed game data through an archive-based organization.
 *
 * KEY FEATURES:
 *
 *   1. ARCHIVE LOADING:
 *      - Loads entire archive files into memory at startup
 *      - Fast random access without disk seeks
 *      - Memory-resident for low-latency retrieval
 *
 *   2. ENTRY MANAGEMENT:
 *      - Metadata tracking for each file in archives
 *      - Name-based lookup system
 *      - Compression information storage
 *
 *   3. FUTURE ENHANCEMENTS:
 *      - Index file parsing (currently TODO)
 *      - Data decompression (currently TODO)
 *      - Hash table for O(1) lookup (currently O(n))
 *
 * IMPLEMENTATION STATUS:
 *
 *   IMPLEMENTED:
 *     - Cache system creation/destruction
 *     - Archive file loading
 *     - Basic retrieval (returns entire archive)
 *     - Memory management
 *
 *   TODO:
 *     - Parse .idx files to extract entry metadata
 *     - Implement file lookup by name
 *     - Add decompression support
 *     - Optimize lookup with hash table
 *
 * MEMORY ARCHITECTURE:
 *
 *   The cache system uses a three-tier memory structure:
 *
 *   Tier 1: CacheSystem (stack or heap)
 *     - Single global instance
 *     - Contains array of Archive structures
 *     - Approximately 2.3 KB
 *
 *   Tier 2: Archive metadata (heap)
 *     - One per archive type (8 total)
 *     - Contains entry arrays
 *     - Approximately 100 KB total
 *
 *   Tier 3: Archive data (heap)
 *     - Actual game asset data
 *     - Largest memory consumer
 *     - 50-200 MB typical
 *
 * ARCHIVE NAME MAPPING:
 *
 *   The archive_names array maps enum values to file names:
 *
 *   Index  Enum                      Name        Purpose
 *   -----  ----                      ----        -------
 *   0      CACHE_ARCHIVE_CONFIG      config      Game definitions
 *   1      CACHE_ARCHIVE_INTERFACE   interface   UI layouts
 *   2      CACHE_ARCHIVE_MEDIA       media       Sprites/fonts
 *   3      CACHE_ARCHIVE_MODELS      models      3D models
 *   4      CACHE_ARCHIVE_SOUNDS      sounds      Audio data
 *   5      CACHE_ARCHIVE_TEXTURES    textures    Texture maps
 *   6      CACHE_ARCHIVE_TITLE       title       Login graphics
 *   7      CACHE_ARCHIVE_WORDENC     wordenc     Text encoding
 *
 *******************************************************************************
 */

#include "cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * GLOBAL VARIABLE: g_cache
 *
 * Global cache system instance. Initialized at server startup.
 * Accessible throughout the codebase via extern declaration in cache.h
 *
 * LIFECYCLE:
 *   1. Initially NULL
 *   2. Set by cache_create() at startup
 *   3. Populated by cache_init()
 *   4. Used throughout server runtime
 *   5. Freed by cache_destroy() at shutdown
 */
CacheSystem* g_cache = NULL;

/*
 * CONSTANT ARRAY: archive_names
 *
 * Maps CacheArchive enum values to their corresponding file names.
 * Used during initialization to construct file paths.
 *
 * ORGANIZATION:
 *   Index matches CacheArchive enum values.
 *   Order must match enum definition in cache.h
 *
 * USAGE:
 *   const char* name = archive_names[CACHE_ARCHIVE_CONFIG];
 *   // name = "config"
 *
 * MEMORY:
 *   8 string pointers (64 bytes on 64-bit systems)
 *   Strings are in read-only data segment
 */
static const char* archive_names[CACHE_ARCHIVE_COUNT] = {
    "config",
    "interface", 
    "media",
    "models",
    "sounds",
    "textures",
    "title",
    "wordenc"
};

/*
 *******************************************************************************
 * CACHE SYSTEM LIFECYCLE FUNCTIONS
 *******************************************************************************
 */

/*
 * FUNCTION: cache_create
 *
 * Allocates a new cache system with zero-initialized fields.
 *
 * IMPLEMENTATION DETAILS:
 *
 *   Uses calloc instead of malloc:
 *     - calloc zeros all memory
 *     - Ensures pointers are NULL
 *     - Ensures counters are 0
 *     - Ensures initialized flag is false
 *
 *   Why zero-initialization matters:
 *     - NULL pointers safe to free
 *     - Zero counts prevent invalid array access
 *     - False initialized prevents premature use
 *
 * MEMORY ALLOCATION:
 *   Size: sizeof(CacheSystem) = approximately 2273 bytes
 *   Components:
 *     - 8 Archive structures (284 bytes each) = 2272 bytes
 *     - 1 bool initialized = 1 byte
 *   Heap location: wherever calloc allocates
 *
 * ERROR HANDLING:
 *   Returns NULL if allocation fails (out of memory)
 *   Caller should check return value before use
 *
 * RETURNS:
 *   Pointer to new CacheSystem or NULL on failure
 */
CacheSystem* cache_create() {
    CacheSystem* cache = calloc(1, sizeof(CacheSystem));
    if (!cache) return NULL;
    
    cache->initialized = false;
    return cache;
}

/*
 * FUNCTION: cache_destroy
 *
 * Frees all memory associated with a cache system.
 *
 * CLEANUP PROCESS:
 *
 *   For each archive:
 *     1. Check if entries array exists (non-NULL)
 *     2. If yes, free entries array
 *     3. Check if data buffer exists (non-NULL)
 *     4. If yes, free data buffer
 *
 *   Finally:
 *     5. Free CacheSystem structure itself
 *
 * MEMORY FREED:
 *
 *   Per archive:
 *     - entries array: entry_count * 76 bytes
 *     - data buffer: data_size bytes (could be MB)
 *
 *   Total:
 *     - Archive entries: approximately 100 KB
 *     - Archive data: 50-200 MB typical
 *     - CacheSystem: approximately 2.3 KB
 *     - Grand total: All cache memory returned to OS
 *
 * NULL SAFETY:
 *   Function safely handles NULL cache parameter
 *   Individual NULL checks for each pointer before free
 *
 * PARAMETERS:
 *   cache - Cache system to destroy (can be NULL)
 */
void cache_destroy(CacheSystem* cache) {
    if (!cache) return;
    
    for (i32 i = 0; i < CACHE_ARCHIVE_COUNT; i++) {
        Archive* archive = &cache->archives[i];
        if (archive->entries) {
            free(archive->entries);
        }
        if (archive->data) {
            free(archive->data);
        }
    }
    
    free(cache);
}

/*
 *******************************************************************************
 * ARCHIVE LOADING FUNCTIONS
 *******************************************************************************
 */

/*
 * FUNCTION: cache_init
 *
 * Initializes the cache system by loading all archives from disk.
 *
 * INITIALIZATION PROCESS:
 *
 *   For each archive type (0 to CACHE_ARCHIVE_COUNT-1):
 *     1. Get archive name from archive_names array
 *     2. Construct full path: "{data_path}/archives/{name}"
 *     3. Call cache_load_archive() to load files
 *     4. Log result (success or failure)
 *
 *   After all archives:
 *     5. Set initialized flag to true
 *     6. Return success
 *
 * PATH CONSTRUCTION:
 *
 *   Example with data_path = "data":
 *     Archive 0 (config):     data/archives/config
 *     Archive 1 (interface):  data/archives/interface
 *     Archive 2 (media):      data/archives/media
 *     ...and so on
 *
 * ERROR HANDLING:
 *
 *   If cache is NULL: return false immediately
 *   If cache already initialized: return false (prevent double-init)
 *   If individual archive fails: log warning but continue
 *     - Non-critical archives can fail without stopping server
 *     - Server may run with partial cache for testing
 *
 * PERFORMANCE:
 *
 *   Time complexity: O(total file size)
 *   Typical time: 100-500 milliseconds for 100 MB
 *   I/O bound: limited by disk read speed
 *
 *   Breakdown per archive:
 *     - fopen: ~1-10 ms (depends on OS cache)
 *     - fseek/ftell: ~1 ms
 *     - malloc: ~1 ms
 *     - fread: ~10-100 ms (depends on file size)
 *     - fclose: ~1 ms
 *
 * PARAMETERS:
 *   cache     - Cache system to initialize
 *   data_path - Base directory for game data
 *
 * RETURNS:
 *   true if initialization succeeds, false otherwise
 */
bool cache_init(CacheSystem* cache, const char* data_path) {
    if (!cache || cache->initialized) return false;
    
    printf("Initializing cache system from %s...\n", data_path);
    
    for (i32 i = 0; i < CACHE_ARCHIVE_COUNT; i++) {
        char archive_path[512];
        snprintf(archive_path, sizeof(archive_path), "%s/archives/%s", 
                 data_path, archive_names[i]);
        
        if (cache_load_archive(&cache->archives[i], archive_path)) {
            printf("Loaded archive: %s\n", archive_names[i]);
        } else {
            printf("Warning: Failed to load archive: %s\n", archive_names[i]);
        }
    }
    
    cache->initialized = true;
    return true;
}

/*
 * FUNCTION: cache_load_archive
 *
 * Loads a single archive file into memory.
 *
 * CURRENT IMPLEMENTATION:
 *
 *   This is a simplified implementation that:
 *     1. Opens the archive file (treated as .dat file)
 *     2. Reads entire file into memory
 *     3. Creates single dummy entry for all data
 *
 *   Does NOT currently:
 *     - Parse .idx index file
 *     - Extract individual file entries
 *     - Support compression/decompression
 *
 * LOADING PROCESS:
 *
 *   Step 1: Open file in binary read mode
 *     fopen(path, "rb")
 *     - "rb" = read, binary mode
 *     - Returns NULL if file doesn't exist
 *
 *   Step 2: Determine file size
 *     fseek(file, 0, SEEK_END) - seek to end
 *     ftell(file) - get position (= file size)
 *     fseek(file, 0, SEEK_SET) - seek back to start
 *
 *   Step 3: Allocate buffer
 *     malloc(file_size)
 *     - Exact size, no extra padding
 *     - Returns NULL if out of memory
 *
 *   Step 4: Read entire file
 *     fread(buffer, 1, file_size, file)
 *     - Reads file_size bytes
 *     - Returns number of bytes actually read
 *     - Should equal file_size on success
 *
 *   Step 5: Close file
 *     fclose(file)
 *     - Releases file handle
 *     - Flushes any buffers
 *
 *   Step 6: Store in Archive structure
 *     archive->data = buffer
 *     archive->data_size = file_size
 *     archive->path = copy of path
 *
 *   Step 7: Create dummy entry (TODO: parse real entries)
 *     entry_count = 1
 *     entries[0].name = "data"
 *     entries[0].offset = 0
 *     entries[0].compressed_size = data_size
 *     entries[0].uncompressed_size = data_size
 *
 * FUTURE ENHANCEMENTS:
 *
 *   Full implementation should:
 *
 *     1. Open .idx file (index)
 *     2. Read entry count
 *     3. For each entry:
 *        a. Read name length
 *        b. Read name string
 *        c. Read data offset
 *        d. Read compressed size
 *        e. Read uncompressed size (if present)
 *     4. Allocate entries array
 *     5. Populate entry structures
 *     6. Open .dat file (data)
 *     7. Read into data buffer
 *
 *   Index file format:
 *     u32: entry_count
 *     For each entry:
 *       u16: name_length
 *       u8[name_length]: name
 *       u32: offset
 *       u32: compressed_size
 *       u32: uncompressed_size
 *
 * ERROR HANDLING:
 *
 *   File doesn't exist:
 *     - fopen returns NULL
 *     - Function returns false
 *
 *   File is empty or invalid:
 *     - ftell returns <= 0
 *     - Close file and return false
 *
 *   Out of memory:
 *     - malloc returns NULL
 *     - Close file and return false
 *
 *   Read fails:
 *     - fread returns < file_size
 *     - Free allocated buffer
 *     - Close file and return false
 *
 * PARAMETERS:
 *   archive - Archive structure to populate
 *   path    - Path to archive file (without extension)
 *
 * RETURNS:
 *   true on success, false on any error
 */
bool cache_load_archive(Archive* archive, const char* path) {
    if (!archive || !path) return false;
    
    FILE* file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    
    /* Get file size */
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size <= 0) {
        fclose(file);
        return false;
    }
    
    /* Allocate and read data */
    archive->data = malloc(file_size);
    if (!archive->data) {
        fclose(file);
        return false;
    }
    
    size_t bytes_read = fread(archive->data, 1, file_size, file);
    fclose(file);
    
    if (bytes_read != file_size) {
        free(archive->data);
        archive->data = NULL;
        return false;
    }
    
    archive->data_size = (u32)file_size;
    strncpy(archive->path, path, sizeof(archive->path) - 1);
    
    /* TODO: parse archive entries */
    archive->entry_count = 1;
    archive->entries = calloc(1, sizeof(CacheEntry));
    if (archive->entries) {
        strcpy(archive->entries[0].name, "data");
        archive->entries[0].offset = 0;
        archive->entries[0].compressed_size = archive->data_size;
        archive->entries[0].uncompressed_size = archive->data_size;
    }
    
    return true;
}

/*
 *******************************************************************************
 * CACHE RETRIEVAL FUNCTIONS
 *******************************************************************************
 */

/*
 * FUNCTION: cache_get_file
 *
 * Retrieves a file from the cache by archive type and name.
 *
 * CURRENT IMPLEMENTATION:
 *
 *   This simplified version:
 *     1. Validates cache and archive type
 *     2. Returns pointer to entire archive data
 *     3. Ignores the name parameter (TODO)
 *
 * VALIDATION CHECKS:
 *
 *   Check 1: Cache exists and is initialized
 *     if (!cache || !cache->initialized)
 *     - Prevents use of uninitialized cache
 *     - Prevents NULL pointer dereference
 *
 *   Check 2: Archive type is valid
 *     if (type >= CACHE_ARCHIVE_COUNT)
 *     - Prevents array out-of-bounds access
 *     - Ensures valid enum value
 *
 *   Check 3: Archive has data
 *     if (archive->data && out_size)
 *     - Ensures data buffer exists
 *     - Ensures output parameter is valid
 *
 * CURRENT BEHAVIOR:
 *
 *   Returns entire archive data buffer regardless of name.
 *   This is a placeholder until full implementation.
 *
 *   Example:
 *     cache_get_file(cache, CACHE_ARCHIVE_CONFIG, "items", &size)
 *     -> Returns all config archive data, not just "items"
 *
 * FULL IMPLEMENTATION ALGORITHM:
 *
 *   Step 1: Validate parameters (same as current)
 *
 *   Step 2: Look up file name in entries array
 *     for (i = 0; i < archive->entry_count; i++):
 *       if (strcmp(archive->entries[i].name, name) == 0):
 *         found_entry = &archive->entries[i]
 *         break
 *
 *   Step 3: If not found, return NULL
 *
 *   Step 4: Extract compressed data
 *     compressed_data = archive->data + found_entry->offset
 *     compressed_size = found_entry->compressed_size
 *
 *   Step 5: Check if decompression needed
 *     if (compressed_size == uncompressed_size):
 *       // No compression, return direct pointer
 *       return compressed_data
 *     else:
 *       // Need decompression
 *       uncompressed = malloc(uncompressed_size)
 *       decompress(compressed_data, compressed_size,
 *                  uncompressed, uncompressed_size)
 *       // Cache decompressed data for reuse
 *       return uncompressed
 *
 * OPTIMIZATION OPPORTUNITIES:
 *
 *   Current: O(n) linear search through entries
 *
 *   Option 1: Hash table lookup
 *     - O(1) average case
 *     - Requires hash table structure
 *     - Memory overhead: pointer array + chain storage
 *
 *   Option 2: Binary search
 *     - O(log n) guaranteed
 *     - Requires sorted entries array
 *     - No memory overhead
 *
 *   Option 3: Decompression cache
 *     - Keep frequently accessed files decompressed
 *     - LRU cache for memory management
 *     - Trades memory for CPU time
 *
 * DECOMPRESSION ALGORITHMS:
 *
 *   Possible compression formats:
 *     - GZIP: Common, good compression, moderate speed
 *     - BZIP2: Better compression, slower
 *     - LZMA: Best compression, slowest
 *     - Custom: Game-specific format
 *
 *   Decompression libraries:
 *     - zlib for GZIP
 *     - libbz2 for BZIP2
 *     - liblzma for LZMA
 *
 * MEMORY MANAGEMENT:
 *
 *   Current: Returns pointer into archive buffer
 *     - No allocation needed
 *     - No free needed
 *     - Pointer valid until cache destroyed
 *
 *   Future with decompression: Returns allocated buffer
 *     - Need allocation strategy
 *     - Need free strategy (who owns buffer?)
 *     - Options:
 *       a. Cache owns, free at cache_destroy
 *       b. Caller owns, must free
 *       c. Reference counted
 *
 * PARAMETERS:
 *   cache    - Cache system to query
 *   type     - Archive type enum value
 *   name     - File name to retrieve (currently ignored)
 *   out_size - Output parameter for data size
 *
 * RETURNS:
 *   Pointer to file data, or NULL if not found
 *   Do NOT free the returned pointer (part of cache)
 */
u8* cache_get_file(CacheSystem* cache, CacheArchive type, const char* name, u32* out_size) {
    if (!cache || !cache->initialized || type >= CACHE_ARCHIVE_COUNT) {
        return NULL;
    }
    
    Archive* archive = &cache->archives[type];
    
    /* For now, just return the entire archive data */
    /* TODO: Implement proper file lookup within archives */
    if (archive->data && out_size) {
        *out_size = archive->data_size;
        return archive->data;
    }
    
    return NULL;
}

/*
 *******************************************************************************
 * EDUCATIONAL NOTES: CACHE DESIGN PATTERNS
 *******************************************************************************
 *
 * DESIGN PATTERN 1: ARCHIVE-BASED STORAGE
 *
 *   Problem: Managing thousands of small game asset files
 *   Solution: Group related files into archives
 *
 *   Benefits:
 *     - Reduced file system overhead
 *     - Faster bulk loading
 *     - Easier distribution
 *     - Better compression (cross-file redundancy)
 *
 *   Real-world examples:
 *     - ZIP/JAR files
 *     - Game .pak files (Quake, Unreal)
 *     - Android .apk files
 *     - iOS .ipa files
 *
 * DESIGN PATTERN 2: INDEX + DATA SEPARATION
 *
 *   Problem: Need fast lookup without loading all data
 *   Solution: Separate index file with metadata
 *
 *   Benefits:
 *     - Quick index parsing
 *     - Random access to data
 *     - Can map data to memory
 *     - Index fits in cache
 *
 *   Real-world examples:
 *     - Database index files (.idx) + data files (.dat)
 *     - Git pack files (.pack) + index files (.idx)
 *     - Video codecs (index frames + compressed data)
 *
 * DESIGN PATTERN 3: MEMORY-RESIDENT CACHE
 *
 *   Problem: Disk I/O latency too high for real-time game
 *   Solution: Load all data into RAM at startup
 *
 *   Benefits:
 *     - Zero latency access (no disk seeks)
 *     - Predictable performance
 *     - Simpler code (no async I/O)
 *
 *   Trade-offs:
 *     - High memory usage (50-200 MB)
 *     - Longer startup time
 *     - Not suitable for very large games
 *
 *   Real-world examples:
 *     - Old-school game engines (pre-streaming era)
 *     - In-memory databases (Redis, Memcached)
 *     - Disk caching systems
 *
 * DESIGN PATTERN 4: LAZY DECOMPRESSION
 *
 *   Problem: Decompressing all files wastes memory
 *   Solution: Keep compressed, decompress on demand
 *
 *   Benefits:
 *     - Lower memory footprint
 *     - Faster startup (skip decompression)
 *     - Only decompress what's needed
 *
 *   Trade-offs:
 *     - CPU cost on first access
 *     - Need cache for decompressed data
 *     - More complex memory management
 *
 *   Real-world examples:
 *     - HTTP compression (gzip transfer)
 *     - Image formats (JPEG, PNG)
 *     - Video streaming (on-the-fly decode)
 *
 * FUTURE OPTIMIZATION IDEAS:
 *
 *   1. Memory-mapped files:
 *      - Use mmap() on Unix, MapViewOfFile() on Windows
 *      - Let OS handle paging
 *      - Reduce memory footprint
 *
 *   2. Compressed archive format:
 *      - Store entire archive compressed
 *      - Decompress individual files on access
 *      - Cache decompressed files in LRU cache
 *
 *   3. Async loading:
 *      - Load archives in background thread
 *      - Allow server to start with partial cache
 *      - Load remaining archives during idle time
 *
 *   4. Hot-reload:
 *      - Watch archive files for changes
 *      - Reload modified archives at runtime
 *      - Useful for development/testing
 *
 *   5. Client-side caching:
 *      - Send cache files to client
 *      - Client caches locally
 *      - Use CRC to validate cache
 *      - Only send updates for changed files
 *
 *******************************************************************************
 */
