/*******************************************************************************
 * CACHE.H - RuneScape Cache System
 *******************************************************************************
 *
 * PURPOSE:
 *   Manages the game's asset cache system, which stores all client-side
 *   resources including graphics, sounds, models, textures, and configuration
 *   data. The cache uses an archive-based format where related assets are
 *   grouped into logical containers for efficient loading and distribution.
 *
 * KEY CONCEPTS:
 *
 *   1. ARCHIVE SYSTEM:
 *      - Multiple archives, each containing related data
 *      - Archives use .dat (data) and .idx (index) file pairs
 *      - Random access to individual files within archives
 *      - Compression support for reduced disk/network usage
 *
 *   2. ARCHIVE TYPES:
 *      - Config: Game configuration (items, NPCs, objects)
 *      - Interface: UI layouts and components
 *      - Media: Sprites, fonts, icons
 *      - Models: 3D model data
 *      - Sounds: Audio effects and music
 *      - Textures: Terrain and object textures
 *      - Title: Login screen and title graphics
 *      - Wordenc: Text filtering/encoding data
 *
 *   3. FILE STRUCTURE:
 *      Each archive consists of two files:
 *        - .dat file: Actual compressed data
 *        - .idx file: Index mapping names to offsets
 *
 *   4. DATA ACCESS PATTERN:
 *      1. Read index to find file offset
 *      2. Seek to offset in data file
 *      3. Read compressed size
 *      4. Read compressed data
 *      5. Decompress if necessary
 *      6. Return uncompressed data to caller
 *
 * ARCHIVE FILE FORMAT:
 *
 *   INDEX FILE (.idx):
 *   +-------------+-------------+-------------+
 *   | Entry Count | Entry 1     | Entry 2     | ...
 *   | (4 bytes)   | (variable)  | (variable)  |
 *   +-------------+-------------+-------------+
 *
 *   Each Index Entry:
 *   +-------------+-------------+-------------+-------------+
 *   | Name Length | Name String | Data Offset | Comp. Size  |
 *   | (2 bytes)   | (N bytes)   | (4 bytes)   | (4 bytes)   |
 *   +-------------+-------------+-------------+-------------+
 *
 *   DATA FILE (.dat):
 *   +-------------+-------------+-------------+
 *   | File 1 Data | File 2 Data | File 3 Data | ...
 *   | (variable)  | (variable)  | (variable)  |
 *   +-------------+-------------+-------------+
 *
 *   Each Data Block (at offset from index):
 *   +-------------+-------------+-------------+
 *   | Uncomp Size | Comp Data   | (padding)   |
 *   | (4 bytes)   | (N bytes)   | (0-3 bytes) |
 *   +-------------+-------------+-------------+
 *
 * CACHE ENTRY STRUCTURE:
 *
 *   Each file within an archive has metadata:
 *     - Name: Identifier string (e.g., "item_definitions")
 *     - Offset: Position in .dat file
 *     - Compressed size: Size of compressed data
 *     - Uncompressed size: Size after decompression
 *
 *   Example entry:
 *     Name: "config_item"
 *     Offset: 1024 (starts at byte 1024 in .dat file)
 *     Compressed: 500 bytes
 *     Uncompressed: 2000 bytes
 *     Compression ratio: 4:1 (75% reduction)
 *
 * ARCHIVE ORGANIZATION:
 *
 *   Config Archive:
 *     - Item definitions (names, stats, values)
 *     - NPC definitions (appearance, combat stats)
 *     - Object definitions (interactive objects)
 *     - Spell definitions (magic system)
 *
 *   Interface Archive:
 *     - UI widget layouts
 *     - Inventory screens
 *     - Dialog boxes
 *     - Menu definitions
 *
 *   Media Archive:
 *     - Sprites (2D images)
 *     - Fonts (text rendering)
 *     - Icons (inventory, skills)
 *     - Background images
 *
 *   Models Archive:
 *     - Character models
 *     - Item models (3D)
 *     - NPC models
 *     - Object models
 *
 *   Sounds Archive:
 *     - Sound effects
 *     - Music tracks
 *     - Ambient sounds
 *
 *   Textures Archive:
 *     - Terrain textures
 *     - Object textures
 *     - Water, sky textures
 *
 *   Title Archive:
 *     - Login screen graphics
 *     - Logo images
 *     - Loading screens
 *
 *   Wordenc Archive:
 *     - Profanity filter data
 *     - Text encoding tables
 *
 * MEMORY LAYOUT:
 *
 *   CacheSystem structure:
 *   +-----------------+
 *   | archives[8]     | --> Array of 8 Archive structures
 *   | initialized     | --> Boolean flag
 *   +-----------------+
 *
 *   Each Archive structure:
 *   +-----------------+
 *   | path[256]       | --> File path to archive
 *   | entries*        | --> Dynamic array of entry metadata
 *   | entry_count     | --> Number of entries
 *   | data*           | --> Entire .dat file in memory
 *   | data_size       | --> Size of data buffer
 *   +-----------------+
 *
 *   Each CacheEntry structure:
 *   +-----------------+
 *   | name[64]        | --> File identifier
 *   | offset          | --> Position in data buffer
 *   | compressed_size | --> Size in data buffer
 *   | uncompressed    | --> Size after decompression
 *   +-----------------+
 *
 * TYPICAL ARCHIVE SIZES:
 *
 *   Archive       Entries    Data Size    Typical Use
 *   -------       -------    ---------    -----------
 *   Config        50-200     100-500 KB   Game definitions
 *   Interface     100-500    500 KB-2 MB  UI layouts
 *   Media         500-2000   2-10 MB      Graphics
 *   Models        1000-5000  5-20 MB      3D models
 *   Sounds        100-500    5-50 MB      Audio (largest)
 *   Textures      100-500    2-10 MB      Texture maps
 *   Title         10-50      1-5 MB       Login graphics
 *   Wordenc       1-10       10-100 KB    Text data
 *
 * COMPRESSION:
 *
 *   Common compression algorithms used:
 *     - GZIP: General-purpose, good ratio
 *     - BZIP2: Better compression, slower
 *     - Custom: Game-specific formats
 *
 *   Compression trade-offs:
 *     - Smaller files: Faster downloads
 *     - CPU cost: Decompression overhead
 *     - Memory: Need buffer for decompressed data
 *
 *   Typical compression ratios:
 *     Config data: 3:1 to 5:1 (70-80% reduction)
 *     Models: 2:1 to 3:1 (50-66% reduction)
 *     Textures: 1.5:1 to 2:1 (33-50% reduction)
 *     Sounds: Already compressed (minimal gain)
 *
 * USAGE WORKFLOW:
 *
 *   1. Server Startup:
 *      - Create cache system
 *      - Load all archives into memory
 *      - Parse index structures
 *
 *   2. Client Request:
 *      - Client asks for specific file
 *      - Server looks up in appropriate archive
 *      - Server retrieves compressed data
 *      - Server sends to client
 *
 *   3. Client Processing:
 *      - Client receives compressed data
 *      - Client decompresses
 *      - Client caches locally
 *      - Client uses uncompressed data
 *
 * PERFORMANCE CHARACTERISTICS:
 *
 *   Cache Initialization:
 *     Time: O(A * E) where A = archives, E = avg entries per archive
 *     Memory: O(total size of all .dat files)
 *     Typical: 50-200 MB loaded at startup
 *
 *   File Lookup:
 *     Time: O(E) linear search through entries
 *     Could be optimized to O(log E) with hash table or binary search
 *     Typical: < 1 microsecond for small archives
 *
 *   File Retrieval:
 *     Time: O(1) direct offset access
 *     Memory: O(compressed size) already in memory
 *
 * USAGE EXAMPLE:
 *
 *   // Server initialization
 *   g_cache = cache_create();
 *   cache_init(g_cache, "data");
 *
 *   // Later, when client needs item definitions
 *   u32 size = 0;
 *   u8* data = cache_get_file(g_cache, CACHE_ARCHIVE_CONFIG, 
 *                              "item_definitions", &size);
 *   if (data) {
 *       // Send 'data' to client (already decompressed)
 *       // 'size' contains the uncompressed size
 *   }
 *
 *   // Cleanup on shutdown
 *   cache_destroy(g_cache);
 *
 * DIRECTORY STRUCTURE:
 *
 *   data/
 *     archives/
 *       config.dat
 *       config.idx
 *       interface.dat
 *       interface.idx
 *       media.dat
 *       media.idx
 *       models.dat
 *       models.idx
 *       sounds.dat
 *       sounds.idx
 *       textures.dat
 *       textures.idx
 *       title.dat
 *       title.idx
 *       wordenc.dat
 *       wordenc.idx
 *
 * EDUCATIONAL NOTES:
 *
 *   Why use archives instead of individual files?
 *     - Reduces file system overhead (fewer inodes, directory entries)
 *     - Faster loading (one open instead of hundreds)
 *     - Easier distribution (fewer files to manage)
 *     - Better compression (can compress across related files)
 *
 *   Why keep entire .dat files in memory?
 *     - Fast random access (no disk seeks)
 *     - Reduced latency for client requests
 *     - Simplifies code (no file handle management)
 *     - Trade-off: Memory for speed
 *
 *   Why separate .idx and .dat files?
 *     - Index can be parsed once at startup
 *     - Data can be memory-mapped efficiently
 *     - Index can be kept small for faster parsing
 *     - Similar to databases (index + data separation)
 *
 *   Real-world analogs:
 *     - ZIP/JAR files: Similar archive concept
 *     - Database systems: Index and data file separation
 *     - Virtual file systems: Archive as virtual directory
 *
 *******************************************************************************
 */

#ifndef CACHE_H
#define CACHE_H

#include "types.h"

/*
 * ENUMERATION: CacheArchive
 *
 * Identifies the different archive types in the cache system.
 * Each archive contains logically related game assets.
 *
 * VALUES:
 *   CACHE_ARCHIVE_CONFIG    - Game configuration data (items, NPCs, objects)
 *   CACHE_ARCHIVE_INTERFACE - User interface layouts and widgets
 *   CACHE_ARCHIVE_MEDIA     - Sprites, fonts, and 2D graphics
 *   CACHE_ARCHIVE_MODELS    - 3D model data for characters and objects
 *   CACHE_ARCHIVE_SOUNDS    - Audio effects and music
 *   CACHE_ARCHIVE_TEXTURES  - Texture maps for 3D rendering
 *   CACHE_ARCHIVE_TITLE     - Login screen and title graphics
 *   CACHE_ARCHIVE_WORDENC   - Word encoding and filtering data
 *   CACHE_ARCHIVE_COUNT     - Total number of archives (for array sizing)
 *
 * USAGE:
 *   cache_get_file(cache, CACHE_ARCHIVE_CONFIG, "item_defs", &size);
 */
typedef enum {
    CACHE_ARCHIVE_CONFIG,
    CACHE_ARCHIVE_INTERFACE,
    CACHE_ARCHIVE_MEDIA,
    CACHE_ARCHIVE_MODELS,
    CACHE_ARCHIVE_SOUNDS,
    CACHE_ARCHIVE_TEXTURES,
    CACHE_ARCHIVE_TITLE,
    CACHE_ARCHIVE_WORDENC,
    CACHE_ARCHIVE_COUNT
} CacheArchive;

/*
 * STRUCTURE: CacheEntry
 *
 * Metadata for a single file within an archive.
 * Contains all information needed to locate and extract the file.
 *
 * FIELDS:
 *   name[64]          - File identifier (null-terminated string)
 *   offset            - Byte offset in archive data file
 *   compressed_size   - Size of compressed data in bytes
 *   uncompressed_size - Size after decompression in bytes
 *
 * MEMORY LAYOUT:
 *   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *   | name[0..63] (64 bytes)                                        |
 *   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *   | offset (4 bytes)          | compressed_size (4 bytes)         |
 *   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *   | uncompressed_size (4 bytes)                                   |
 *   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *   Total: 76 bytes per entry
 *
 * EXAMPLE:
 *   CacheEntry entry;
 *   strcpy(entry.name, "item_definitions");
 *   entry.offset = 1024;
 *   entry.compressed_size = 500;
 *   entry.uncompressed_size = 2000;
 *
 *   Interpretation:
 *     - File "item_definitions" starts at byte 1024 in data file
 *     - Compressed data is 500 bytes long
 *     - After decompression, data will be 2000 bytes
 *     - Compression ratio: 4:1 (75% space savings)
 */
typedef struct {
    char name[64];
    u32 offset;
    u32 compressed_size;
    u32 uncompressed_size;
} CacheEntry;

/*
 * STRUCTURE: Archive
 *
 * Represents a single cache archive with its index and data.
 * Archives group related files together for efficient access.
 *
 * FIELDS:
 *   path[256]   - File path to the archive (without .dat/.idx extension)
 *   entries     - Dynamic array of file metadata (heap-allocated)
 *   entry_count - Number of files in this archive
 *   data        - Entire .dat file contents (heap-allocated)
 *   data_size   - Total size of data buffer in bytes
 *
 * MEMORY LAYOUT:
 *   +---+---+---+---+---+---+---+---+
 *   | path[0..255] (256 bytes)      |
 *   +---+---+---+---+---+---+---+---+
 *   | entries* (8 bytes pointer)    |
 *   +---+---+---+---+---+---+---+---+
 *   | entry_count (4 bytes)         |
 *   +---+---+---+---+---+---+---+---+
 *   | data* (8 bytes pointer)       |
 *   +---+---+---+---+---+---+---+---+
 *   | data_size (4 bytes)           |
 *   +---+---+---+---+---+---+---+---+
 *   Total: 284 bytes per archive (plus heap allocations)
 *
 * HEAP ALLOCATIONS:
 *   entries: entry_count * sizeof(CacheEntry) bytes
 *   data: data_size bytes
 *
 * EXAMPLE:
 *   Archive* config = &cache->archives[CACHE_ARCHIVE_CONFIG];
 *   strcpy(config->path, "data/archives/config");
 *   config->entry_count = 100;
 *   config->entries = malloc(100 * sizeof(CacheEntry));
 *   config->data_size = 500000;  // 500 KB
 *   config->data = malloc(500000);
 *
 * LIFECYCLE:
 *   1. Allocate Archive structure
 *   2. Load and parse .idx file -> populate entries
 *   3. Load .dat file -> populate data buffer
 *   4. Ready for cache_get_file() calls
 *   5. On shutdown: free entries, free data
 */
typedef struct {
    char path[256];
    CacheEntry* entries;
    u32 entry_count;
    u8* data;
    u32 data_size;
} Archive;

/*
 * STRUCTURE: CacheSystem
 *
 * Main cache management structure containing all archives.
 * Global instance provides access to all game assets.
 *
 * FIELDS:
 *   archives[CACHE_ARCHIVE_COUNT] - Array of 8 archive structures
 *   initialized                    - True after cache_init() succeeds
 *
 * MEMORY LAYOUT:
 *   +---+---+---+---+---+---+---+---+
 *   | archives[0] (284 bytes)       |  Config archive
 *   +---+---+---+---+---+---+---+---+
 *   | archives[1] (284 bytes)       |  Interface archive
 *   +---+---+---+---+---+---+---+---+
 *   | archives[2] (284 bytes)       |  Media archive
 *   +---+---+---+---+---+---+---+---+
 *   | archives[3] (284 bytes)       |  Models archive
 *   +---+---+---+---+---+---+---+---+
 *   | archives[4] (284 bytes)       |  Sounds archive
 *   +---+---+---+---+---+---+---+---+
 *   | archives[5] (284 bytes)       |  Textures archive
 *   +---+---+---+---+---+---+---+---+
 *   | archives[6] (284 bytes)       |  Title archive
 *   +---+---+---+---+---+---+---+---+
 *   | archives[7] (284 bytes)       |  Wordenc archive
 *   +---+---+---+---+---+---+---+---+
 *   | initialized (1 byte)          |
 *   +---+---+---+---+---+---+---+---+
 *   Total: approximately 2273 bytes (plus heap allocations)
 *
 * TOTAL MEMORY USAGE:
 *   Base structure: ~2.3 KB
 *   Archive data: 50-200 MB (depends on game assets)
 *   Archive entries: ~100 KB (depends on file count)
 *
 * EXAMPLE:
 *   CacheSystem* cache = cache_create();
 *   // cache->initialized is false
 *   cache_init(cache, "data");
 *   // cache->initialized is now true
 *   // All 8 archives are loaded and ready
 */
typedef struct {
    Archive archives[CACHE_ARCHIVE_COUNT];
    bool initialized;
} CacheSystem;

/*
 * GLOBAL VARIABLE: g_cache
 *
 * Global cache system instance accessible throughout the server.
 * Initialized at server startup, destroyed at shutdown.
 *
 * USAGE:
 *   extern CacheSystem* g_cache;
 *   u8* data = cache_get_file(g_cache, CACHE_ARCHIVE_CONFIG, "items", &size);
 *
 * LIFECYCLE:
 *   1. main(): g_cache = cache_create()
 *   2. main(): cache_init(g_cache, "data")
 *   3. Throughout runtime: cache_get_file(g_cache, ...)
 *   4. shutdown(): cache_destroy(g_cache)
 */
extern CacheSystem* g_cache;

/*
 * FUNCTION: cache_create
 *
 * Allocates and initializes a new cache system.
 * Archives are empty until cache_init() is called.
 *
 * PROCESS:
 *   1. Allocate CacheSystem structure with calloc
 *   2. Zero-initialize all fields
 *   3. Set initialized flag to false
 *   4. Return pointer to new structure
 *
 * RETURNS:
 *   Pointer to new CacheSystem, or NULL if allocation fails
 *
 * MEMORY:
 *   Allocates approximately 2.3 KB on heap
 *
 * EXAMPLE:
 *   CacheSystem* cache = cache_create();
 *   if (!cache) {
 *       fprintf(stderr, "Failed to create cache\n");
 *       return -1;
 *   }
 */
CacheSystem* cache_create();

/*
 * FUNCTION: cache_destroy
 *
 * Frees all memory associated with a cache system.
 * Should be called at server shutdown.
 *
 * PROCESS:
 *   1. For each archive:
 *      a. Free entries array if allocated
 *      b. Free data buffer if allocated
 *   2. Free CacheSystem structure itself
 *
 * MEMORY FREED:
 *   - All archive data buffers (50-200 MB typically)
 *   - All entry arrays (~100 KB typically)
 *   - CacheSystem structure (~2.3 KB)
 *   - Total: All cache-related memory returned to system
 *
 * PARAMETERS:
 *   cache - Pointer to cache system to destroy (can be NULL)
 *
 * EXAMPLE:
 *   cache_destroy(g_cache);
 *   g_cache = NULL;
 */
void cache_destroy(CacheSystem* cache);

/*
 * FUNCTION: cache_init
 *
 * Loads all cache archives from disk into memory.
 * This is called once at server startup.
 *
 * PROCESS:
 *   1. For each archive type (config, interface, media, etc.):
 *      a. Construct path: "{data_path}/archives/{name}"
 *      b. Call cache_load_archive() to load .idx and .dat
 *      c. Log success or warning
 *   2. Set initialized flag to true
 *   3. Return success
 *
 * PARAMETERS:
 *   cache     - Cache system to initialize
 *   data_path - Base directory containing archives/ subdirectory
 *
 * RETURNS:
 *   true if initialization succeeds, false otherwise
 *
 * TIME COMPLEXITY:
 *   O(total size of all archive files)
 *   Typical: 100-500 milliseconds for 100 MB of data
 *
 * EXAMPLE:
 *   CacheSystem* cache = cache_create();
 *   if (!cache_init(cache, "data")) {
 *       fprintf(stderr, "Failed to initialize cache\n");
 *       cache_destroy(cache);
 *       return -1;
 *   }
 *   printf("Cache initialized successfully\n");
 */
bool cache_init(CacheSystem* cache, const char* data_path);

/*
 * FUNCTION: cache_load_archive
 *
 * Loads a single archive from disk into memory.
 * Reads both the .idx (index) and .dat (data) files.
 *
 * PROCESS:
 *   1. Open archive file (currently just loads .dat file)
 *   2. Determine file size using fseek/ftell
 *   3. Allocate buffer of exact size
 *   4. Read entire file into buffer
 *   5. Close file
 *   6. Store buffer and size in Archive structure
 *   7. TODO: Parse .idx file to populate entries array
 *
 * CURRENT IMPLEMENTATION:
 *   Currently creates a single dummy entry containing all data.
 *   Full implementation would:
 *     1. Read .idx file to get entry metadata
 *     2. Allocate entries array
 *     3. Populate each entry with name, offset, sizes
 *     4. Read .dat file for actual data
 *
 * PARAMETERS:
 *   archive - Archive structure to populate
 *   path    - Base path to archive (without extension)
 *
 * RETURNS:
 *   true if successful, false on any error
 *
 * ERROR HANDLING:
 *   - File doesn't exist: return false
 *   - File is empty: return false
 *   - Memory allocation fails: return false
 *   - Read fails: free allocated memory, return false
 *
 * EXAMPLE:
 *   Archive config_archive;
 *   if (cache_load_archive(&config_archive, "data/archives/config")) {
 *       printf("Loaded config archive: %u bytes\n", config_archive.data_size);
 *   }
 */
bool cache_load_archive(Archive* archive, const char* path);

/*
 * FUNCTION: cache_get_file
 *
 * Retrieves a file from the cache by archive type and name.
 * Returns pointer to uncompressed data.
 *
 * PROCESS:
 *   1. Validate cache is initialized
 *   2. Validate archive type is in range
 *   3. Get reference to requested archive
 *   4. Search entries for matching name (TODO: currently returns all data)
 *   5. Decompress if necessary (TODO: not yet implemented)
 *   6. Return pointer to data and set output size
 *
 * CURRENT IMPLEMENTATION:
 *   Returns pointer to entire archive data buffer.
 *   Full implementation would:
 *     1. Look up file name in entries array
 *     2. Seek to offset in data buffer
 *     3. Extract compressed data
 *     4. Decompress if compressed_size != uncompressed_size
 *     5. Return decompressed data (possibly cached)
 *
 * PARAMETERS:
 *   cache    - Cache system to query
 *   type     - Archive type to search in
 *   name     - File name to look up
 *   out_size - Output parameter for file size (set by function)
 *
 * RETURNS:
 *   Pointer to file data, or NULL if not found
 *   The returned pointer is valid until cache is destroyed
 *   Do NOT free the returned pointer (it's part of the archive buffer)
 *
 * TIME COMPLEXITY:
 *   Current: O(1) - direct access
 *   Full implementation: O(n) - linear search through entries
 *   Could be O(log n) with binary search or O(1) with hash table
 *
 * EXAMPLE:
 *   u32 size = 0;
 *   u8* item_data = cache_get_file(g_cache, CACHE_ARCHIVE_CONFIG, 
 *                                   "item_definitions", &size);
 *   if (item_data) {
 *       printf("Item data: %u bytes\n", size);
 *       // Use item_data...
 *       // Do NOT free item_data - it's part of cache
 *   } else {
 *       printf("Item definitions not found\n");
 *   }
 */
u8* cache_get_file(CacheSystem* cache, CacheArchive type, const char* name, u32* out_size);

#endif /* CACHE_H */
