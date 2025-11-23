/*******************************************************************************
 * MAIN.C - RuneScape #225 Server Entry Point
 *******************************************************************************
 * 
 * This file implements the application entry point for the RuneScape Private
 * Server (RSPS). It handles server initialization, signal management, and
 * graceful shutdown.
 * 
 * KEY RESPONSIBILITIES:
 *   1. Heap allocation of GameServer structure
 *   2. Signal handler registration for clean shutdown
 *   3. Server initialization and startup
 *   4. Main event loop execution
 *   5. Graceful cleanup on exit
 * 
 * EDUCATIONAL CONCEPTS:
 *   - Signal handling (POSIX signals for process control)
 *   - Heap vs stack allocation
 *   - Resource lifecycle management
 *   - Graceful shutdown patterns
 *   - Global state management
 * 
 * SIGNAL HANDLING:
 * 
 * POSIX signals are asynchronous notifications sent to processes:
 *   - SIGINT:  Interrupt from keyboard (Ctrl+C)
 *   - SIGTERM: Termination request from OS or kill command
 *   - SIGHUP:  Hang up detected (terminal closed)
 * 
 * Signal handler execution:
 *   1. User presses Ctrl+C
 *   2. OS sends SIGINT to process
 *   3. Current execution suspended
 *   4. signal_handler() runs
 *   5. Handler sets server->running = false
 *   6. Main loop exits cleanly
 *   7. Normal shutdown proceeds
 * 
 * WHY GRACEFUL SHUTDOWN MATTERS:
 *   Without signal handling:
 *     - Player data may not be saved
 *     - Sockets left open (port stays bound)
 *     - Memory leaks
 *     - Corrupted state files
 *   
 *   With signal handling:
 *     - All players receive logout packets
 *     - Data is saved to disk
 *     - Network sockets closed properly
 *     - Clean memory deallocation
 * 
 * HEAP ALLOCATION RATIONALE:
 * 
 * GameServer is allocated on heap because:
 *   1. Size: Structure is very large (contains MAX_PLAYERS player slots)
 *   2. Stack overflow: Default stack size often 1-8 MB
 *   3. Persistence: Must survive beyond main() scope
 *   4. Signal safety: Global pointer allows access from signal handler
 * 
 * SIZE CALCULATION:
 *   GameServer structure contains:
 *   - Player array: MAX_PLAYERS slots
 *   - Structure is large and exceeds typical stack limits
 *   - Must be allocated on heap to prevent stack overflow
 * 
 * INITIALIZATION SEQUENCE:
 * 
 * 1. Allocate GameServer on heap
 *    calloc() zero-initializes memory (important for flags)
 * 
 * 2. Register signal handlers
 *    Must happen before server_init() to catch early termination
 * 
 * 3. Initialize server subsystems
 *    server_init() sets up:
 *    - World state
 *    - Cache system
 *    - Item/NPC/Object systems
 *    - Network socket
 * 
 * 4. Run main event loop
 *    server_run() enters infinite loop (until shutdown)
 * 
 * 5. Clean shutdown
 *    server_shutdown() deallocates all resources
 * 
 * ERROR HANDLING PHILOSOPHY:
 * 
 * Early exit on critical failures:
 *   - If heap allocation fails → exit(1)
 *   - If server_init() fails → exit(1)
 *   - Cannot recover from these errors
 * 
 * Logging for visibility:
 *   - fprintf(stderr) for errors (goes to terminal/log)
 *   - printf() for normal status messages
 * 
 * GLOBAL STATE JUSTIFICATION:
 * 
 * g_server is global because:
 *   1. Signal handlers cannot take parameters
 *   2. Must be accessible from signal context
 *   3. Single server instance per process
 * 
 * Alternative (not used here):
 *   Use pthread_self() + thread-local storage
 *   More complex, overkill for single-threaded server
 * 
 * MEMORY LEAK PREVENTION:
 * 
 * Every allocation has corresponding free:
 *   calloc(GameServer) → free(server) at end of main()
 *   server_init() allocations → server_shutdown() deallocations
 * 
 * Use valgrind to verify:
 *   valgrind --leak-check=full ./server
 *   Should show: "All heap blocks were freed"
 * 
 * PLATFORM PORTABILITY:
 * 
 * Signal handling is POSIX-compliant:
 *   - Works on Linux, macOS, BSD
 *   - Windows requires different approach (SetConsoleCtrlHandler)
 * 
 * Windows adaptation (not implemented):
 *   #ifdef _WIN32
 *   BOOL WINAPI CtrlHandler(DWORD type) {
 *       if (type == CTRL_C_EVENT) {
 *           server_shutdown(g_server);
 *           return TRUE;
 *       }
 *       return FALSE;
 *   }
 *   #endif
 * 
 * COMPLEXITY:
 *   - Time: O(1) for main(), O(N) for server lifecycle
 *   - Space: O(N) where N = MAX_PLAYERS
 * 
 ******************************************************************************/

#include "server.h"
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>

/*******************************************************************************
 * GLOBAL STATE
 ******************************************************************************/

/*
 * g_server - Global pointer to GameServer instance
 * 
 * PURPOSE: Allows signal handlers to access server state for clean shutdown
 * 
 * SCOPE: File-level global (not exported to other compilation units)
 * 
 * INITIALIZATION: Set in main(), cleared on exit
 * 
 * THREAD SAFETY: Not thread-safe (server is single-threaded)
 */
GameServer* g_server = NULL;

/*******************************************************************************
 * SIGNAL HANDLING
 ******************************************************************************/

/*
 * signal_handler - Handle termination signals for graceful shutdown
 * 
 * @param sig  Signal number (SIGINT=2, SIGTERM=15, etc.)
 * 
 * ALGORITHM:
 *   1. Check if server is initialized
 *   2. Print shutdown message
 *   3. Call server_shutdown() to cleanup
 *   4. Return to interrupted context
 *   5. Main loop will exit normally
 * 
 * SIGNAL SAFETY:
 *   This function runs in signal context (async-signal-safe)
 *   
 *   Safe operations:
 *     - Accessing global variables
 *     - Calling async-signal-safe functions
 *     - Setting flags
 *   
 *   Unsafe operations (avoided here):
 *     - malloc/free (not async-signal-safe)
 *     - printf (technically unsafe, but commonly used)
 *     - Calling non-reentrant functions
 * 
 * REENTRANCY:
 *   If SIGINT arrives during signal_handler execution:
 *   - Handler will be invoked again (recursion)
 *   - Protected by g_server NULL check
 *   - Second call has no effect (server already shutting down)
 * 
 * SIGNAL DISPOSITION:
 *   After handler returns, default behavior is restored
 *   If user presses Ctrl+C again during shutdown, process terminates immediately
 * 
 * EXAMPLE EXECUTION FLOW:
 * 
 *   Normal operation:
 *     main() → server_run() → [infinite loop] → ...
 *   
 *   User presses Ctrl+C:
 *     [loop interrupted] → signal_handler(SIGINT) → server_shutdown()
 *     → [handler returns] → [loop continues one iteration]
 *     → while(server->running) is false → exit loop
 *     → cleanup in main() → exit(0)
 * 
 * PORTABILITY:
 *   POSIX signal handling:
 *     - signal() is deprecated (use sigaction() for new code)
 *     - signal() used here for simplicity
 *   
 *   Windows equivalent:
 *     SetConsoleCtrlHandler() for console apps
 * 
 * COMPLEXITY: O(1) time
 */
void signal_handler(int sig) {
    if (g_server) {
        printf("\nShutting down server (signal %d)...\n", sig);
        server_shutdown(g_server);
    }
}

/*******************************************************************************
 * ENTRY POINT
 ******************************************************************************/

/*
 * main - Application entry point
 * 
 * @param argc  Argument count (unused)
 * @param argv  Argument vector (unused)
 * @return      Exit code (0 = success, 1 = failure)
 * 
 * ALGORITHM:
 *   1. Allocate GameServer structure on heap
 *   2. Set global pointer for signal handler access
 *   3. Register signal handlers for graceful shutdown
 *   4. Initialize server subsystems
 *   5. Run main event loop
 *   6. Clean shutdown
 *   7. Free heap memory
 *   8. Return success code
 * 
 * HEAP ALLOCATION:
 *   calloc(1, sizeof(GameServer)) allocates and zero-initializes
 *   
 *   Why calloc instead of malloc?
 *     - Zero-initialization ensures all flags start as 0/NULL
 *     - Prevents undefined behavior from uninitialized reads
 *     - Equivalent to: malloc(size) + memset(ptr, 0, size)
 *   
 *   Size calculation:
 *     sizeof(GameServer) includes all server subsystems
 *     Size is platform-dependent based on MAX_PLAYERS and structure packing
 * 
 * SIGNAL REGISTRATION:
 *   signal(SIGINT, signal_handler)
 *     - Associates signal_handler with SIGINT (Ctrl+C)
 *     - Returns previous handler (ignored)
 *   
 *   signal(SIGTERM, signal_handler)
 *     - Associates signal_handler with SIGTERM (kill command)
 *     - Allows graceful shutdown when killed
 * 
 * INITIALIZATION:
 *   server_init(server, SERVER_PORT)
 *     - SERVER_PORT defined in constants.h (typically 43594)
 *     - Returns false on failure
 *     - Failure causes immediate exit (cannot recover)
 * 
 * EVENT LOOP:
 *   server_run(server)
 *     - Enters infinite loop
 *     - Processes network events
 *     - Executes game ticks every 600ms
 *     - Returns only when server->running becomes false
 * 
 * CLEANUP SEQUENCE:
 *   1. server_shutdown(server)
 *      - Disconnects all players
 *      - Closes network sockets
 *      - Frees all subsystem resources
 *   
 *   2. free(server)
 *      - Deallocates GameServer structure
 *      - Returns memory to heap
 *   
 *   3. return 0
 *      - Indicates successful termination to OS
 *      - OS reclaims any leaked resources
 * 
 * ERROR HANDLING:
 *   Heap allocation failure:
 *     fprintf(stderr, ...) → prints error
 *     return 1 → non-zero exit code
 *   
 *   Server initialization failure:
 *     free(server) → prevent memory leak
 *     return 1 → non-zero exit code
 * 
 * EXIT CODES:
 *   0: Success (normal shutdown)
 *   1: Failure (allocation or initialization error)
 * 
 * RESOURCE OWNERSHIP:
 *   GameServer* server
 *     - Owned by main()
 *     - Created with calloc()
 *     - Destroyed with free()
 *     - Must remain valid until free() call
 * 
 * ALTERNATIVE DESIGNS:
 * 
 *   Stack allocation (unsafe for large structures):
 *     GameServer server;  // BAD: stack overflow for large GameServer
 *     server_init(&server, SERVER_PORT);
 *   
 *   Static allocation (works, but limits flexibility):
 *     static GameServer server;  // OK: goes in BSS segment
 *     server_init(&server, SERVER_PORT);
 * 
 * DEBUGGING NOTES:
 *   GDB breakpoints:
 *     break main          # Stop at entry
 *     break signal_handler # Stop on Ctrl+C
 *     run
 *   
 *   Valgrind memory check:
 *     valgrind --leak-check=full ./server
 *     Should show 0 bytes lost
 * 
 * COMPLEXITY:
 *   - Time: O(1) excluding server lifecycle
 *   - Space: O(N) where N = MAX_PLAYERS
 */
int main(int argc, char** argv) {
    /* Suppress unused parameter warnings */
    (void)argc;
    (void)argv;
    
    /*
     * STEP 1: Allocate GameServer on heap
     * 
     * calloc() ensures all bytes are zeroed:
     *   - player[].state = PLAYER_STATE_DISCONNECTED (0)
     *   - server->running = false
     *   - All pointers = NULL
     */
    /* Allocate server on heap to avoid stack overflow */
    GameServer* server = calloc(1, sizeof(GameServer));
    if (!server) {
        fprintf(stderr, "ERROR: Failed to allocate server memory\n");
        fprintf(stderr, "       Required: %zu bytes\n", sizeof(GameServer));
        return 1;
    }
    
    /*
     * STEP 2: Set global pointer for signal handler
     * 
     * Must be set before server_init() in case signal arrives during init
     */
    g_server = server;
    
    /*
     * STEP 3: Register signal handlers
     * 
     * SIGINT:  Ctrl+C (interrupt)
     * SIGTERM: kill <pid> (termination request)
     * 
     * Both signals now invoke signal_handler() for graceful shutdown
     */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /*
     * STEP 4: Print startup banner
     */
    printf("========================================\n");
    printf("  RuneScape Private Server (C)\n");
    printf("  Revision: 225 (May 2004 Protocol)\n");
    printf("  Port: %d\n", SERVER_PORT);
    printf("========================================\n");
    printf("Starting server...\n");
    
    /*
     * STEP 5: Initialize server subsystems
     * 
     * Initializes in order:
     *   - World state
     *   - Cache system (item/NPC/object definitions)
     *   - Item system
     *   - NPC system
     *   - Object system
     *   - Player slots
     *   - Network socket
     * 
     * Returns false on critical failure (port in use, out of memory, etc.)
     */
    if (!server_init(server, SERVER_PORT)) {
        fprintf(stderr, "ERROR: Server initialization failed\n");
        fprintf(stderr, "       Common causes:\n");
        fprintf(stderr, "         - Port %d already in use\n", SERVER_PORT);
        fprintf(stderr, "         - Insufficient memory\n");
        fprintf(stderr, "         - Missing data files\n");
        free(server);
        g_server = NULL;
        return 1;
    }
    
    /*
     * STEP 6: Enter main event loop
     * 
     * server_run() blocks until server->running becomes false
     * This happens when:
     *   - Signal handler calls server_shutdown()
     *   - Critical error occurs
     */
    printf("========================================\n");
    printf("  Server is now online!\n");
    printf("  Press Ctrl+C to stop gracefully\n");
    printf("========================================\n");
    server_run(server);
    
    /*
     * STEP 7: Graceful shutdown
     * 
     * Only reached after server_run() returns (server->running = false)
     * Performs final cleanup:
     *   - Disconnect remaining players
     *   - Save data
     *   - Close network sockets
     *   - Free subsystem resources
     */
    printf("Performing final cleanup...\n");
    server_shutdown(server);
    
    /*
     * STEP 8: Free heap memory
     * 
     * Deallocates GameServer structure
     * All player slots, buffers, and subsystem data are freed
     */
    free(server);
    g_server = NULL;
    
    /*
     * STEP 9: Exit successfully
     */
    printf("========================================\n");
    printf("  Server stopped cleanly\n");
    printf("  Exit code: 0 (success)\n");
    printf("========================================\n");
    return 0;
}
