/*******************************************************************************
 * NETWORK.C - TCP/IP Socket Server Implementation
 *******************************************************************************
 * 
 * This file implements a cross-platform TCP server using Berkeley Sockets API.
 * 
 * KEY ALGORITHMS IMPLEMENTED:
 *   1. Server socket lifecycle (socket → bind → listen → accept)
 *   2. Non-blocking I/O for concurrent client handling
 *   3. Cross-platform abstractions (POSIX vs Winsock)
 *   4. TCP connection management and graceful shutdown
 * 
 * EDUCATIONAL CONCEPTS:
 *   - Berkeley Sockets API (standard network programming interface)
 *   - TCP protocol (reliable, connection-oriented transport)
 *   - Non-blocking I/O (polling without threads)
 *   - Platform-specific networking (Windows vs POSIX differences)
 *   - Socket options (SO_REUSEADDR for port reuse)
 *   - Network byte order (endianness conversion)
 * 
 * CROSS-PLATFORM COMPATIBILITY:
 * 
 * This code compiles on:
 *   - Linux (uses POSIX sockets via <sys/socket.h>)
 *   - macOS (uses POSIX sockets via <sys/socket.h>)
 *   - Windows (uses Winsock2 via <winsock2.h>)
 * 
 * Platform detection via preprocessor:
 *   #ifdef _WIN32  → Windows-specific code
 *   #else          → POSIX-specific code (Linux, macOS, BSD)
 * 
 ******************************************************************************/

#include "network.h"
#include <string.h>
#include <stdio.h>   /* for printf in dump_hex */

/* Platform-specific headers */
#ifdef _WIN32
    /* Windows Sockets (Winsock2) */
    #include <winsock2.h>   /* WSAStartup, socket, bind, listen, accept, etc. */
    #include <ws2tcpip.h>   /* IP address conversion functions */
    #pragma comment(lib, "ws2_32.lib")  /* Link against Winsock library */
#else
    /* POSIX Sockets (Linux, macOS, BSD) */
    #include <sys/socket.h> /* socket, bind, listen, accept, recv, send */
    #include <netinet/in.h> /* sockaddr_in, INADDR_ANY, htons */
    #include <fcntl.h>      /* fcntl for non-blocking mode */
    #include <unistd.h>     /* close */
#endif

/*******************************************************************************
 * DEBUG UTILITIES
 ******************************************************************************/

/*
 * dump_hex - Print byte array as hexadecimal (for protocol debugging)
 * 
 * @param tag   Descriptive label (e.g., "TX", "RX")
 * @param data  Byte array to dump
 * @param len   Number of bytes to print
 * 
 * OUTPUT FORMAT:
 *   [HEX] TX len=10: 01 02 03 04 05 06 07 08 09 0A
 * 
 * USE CASES:
 *   - Verify packet structure during development
 *   - Debug protocol mismatches (compare with expected bytes)
 *   - Analyze captured network traffic (e.g., Wireshark dumps)
 * 
 * EXAMPLE:
 *   u8 packet[] = {0x01, 0x02, 0x03};
 *   dump_hex("SEND", packet, 3);
 *   // Output: [HEX] SEND len=3: 01 02 03
 * 
 * COMPLEXITY: O(len) time
 */
static void dump_hex(const char* tag, const u8* data, u32 len) {
    printf("[HEX] %s len=%u: ", tag, len);
    for (u32 i = 0; i < len; ++i) {
        printf("%02X ", data[i]);  /* %02X = 2-digit hex with leading zero */
    }
    printf("\n");
}

/*******************************************************************************
 * SERVER INITIALIZATION
 ******************************************************************************/

/*
 * network_init - Initialize TCP server and start listening for connections
 * 
 * @param server  Pointer to NetworkServer structure to initialize
 * @param port    TCP port to bind (0-65535, typically 43594 for RuneScape)
 * @return        true on success, false on any error
 * 
 * ALGORITHM:
 *   1. [Windows only] Initialize Winsock library (WSAStartup)
 *   2. Create TCP socket (AF_INET, SOCK_STREAM)
 *   3. Set SO_REUSEADDR option (allows immediate port reuse)
 *   4. Set non-blocking mode (fcntl/ioctlsocket)
 *   5. Bind socket to 0.0.0.0:port (all network interfaces)
 *   6. Start listening with backlog=10 (queue 10 pending connections)
 *   7. Mark server as running
 * 
 * DETAILED STEPS:
 * 
 * STEP 1: WINSOCK INITIALIZATION (Windows only)
 * ──────────────────────────────────────────────────────────────────
 * WSAStartup(MAKEWORD(2, 2), &wsa_data)
 * 
 * Purpose: Load Winsock DLL and verify version compatibility
 * 
 * MAKEWORD(2, 2): Request Winsock version 2.2
 *   - Major version: 2
 *   - Minor version: 2
 *   - Winsock 2.2 is standard on Windows XP and later
 * 
 * WSADATA structure contains:
 *   - wVersion:      Version of Winsock actually loaded
 *   - wHighVersion:  Highest version supported
 *   - szDescription: Human-readable description
 *   - szSystemStatus: Status information
 * 
 * Why needed on Windows:
 *   - Winsock is a DLL, not built into kernel like POSIX sockets
 *   - Must initialize before any socket calls
 *   - Returns 0 on success, non-zero on error
 * 
 * STEP 2: SOCKET CREATION
 * ──────────────────────────────────────────────────────────────────
 * socket(AF_INET, SOCK_STREAM, 0)
 * 
 * AF_INET: Address Family Internet (IPv4)
 *   - Uses 32-bit IP addresses (e.g., 192.168.1.100)
 *   - AF_INET6 would be for IPv6 (128-bit addresses)
 * 
 * SOCK_STREAM: Connection-oriented byte stream (TCP)
 *   - Reliable delivery (automatic retransmission)
 *   - In-order delivery (packets arrive in sequence)
 *   - Connection-oriented (requires handshake)
 *   - Alternative: SOCK_DGRAM for UDP (connectionless)
 * 
 * Protocol 0: Auto-select protocol
 *   - For AF_INET + SOCK_STREAM, always selects TCP
 *   - Could explicitly specify IPPROTO_TCP
 * 
 * Returns:
 *   >= 0: Valid socket file descriptor (UNIX: int, Windows: SOCKET)
 *   -1:   Error (out of file descriptors, no network support)
 * 
 * SOCKET FILE DESCRIPTOR (UNIX):
 *   - Kernel allocates lowest available fd (typically 3+ since 0-2 are stdin/stdout/stderr)
 *   - Stored in process file descriptor table
 *   - Can use read(), write(), close() like regular files
 * 
 * SOCKET HANDLE (Windows):
 *   - SOCKET type is UINT_PTR (pointer-sized unsigned integer)
 *   - NOT compatible with ReadFile/WriteFile (use recv/send instead)
 *   - Must use closesocket() instead of CloseHandle()
 * 
 * STEP 3: SET SO_REUSEADDR OPTION
 * ──────────────────────────────────────────────────────────────────
 * setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))
 * 
 * Purpose: Allow binding to port in TIME_WAIT state
 * 
 * SO_REUSEADDR effect:
 *   - Without flag: bind() fails with EADDRINUSE if port recently used
 *   - With flag:    bind() succeeds even if port in TIME_WAIT state
 * 
 * TIME_WAIT STATE:
 *   After close(), TCP keeps port reserved for 2*MSL (typically 60 seconds)
 *   Prevents delayed packets from previous connection affecting new one
 *   
 *   Problem: Server restart within 60s fails to bind
 *   Solution: SO_REUSEADDR allows immediate rebinding
 * 
 * SOCKET OPTION LEVELS:
 *   SOL_SOCKET:  Socket-level options (apply to all protocols)
 *   IPPROTO_TCP: TCP-specific options (e.g., TCP_NODELAY)
 *   IPPROTO_IP:  IP-level options (e.g., IP_TTL)
 * 
 * PLATFORM CAST DIFFERENCE:
 *   UNIX:    (void*) &opt  (setsockopt expects void*)
 *   Windows: (char*) &opt  (Winsock expects char*)
 * 
 * STEP 4: SET NON-BLOCKING MODE
 * ──────────────────────────────────────────────────────────────────
 * UNIX:    fcntl(server_fd, F_SETFL, O_NONBLOCK)
 * Windows: ioctlsocket(server_fd, FIONBIO, &mode) where mode=1
 * 
 * BLOCKING MODE (default):
 *   accept() waits until client connects (thread sleeps)
 *   recv()   waits until data arrives
 *   send()   waits until buffer space available
 *   
 *   Problem: One slow operation blocks entire server
 * 
 * NON-BLOCKING MODE:
 *   accept() returns -1 immediately if no pending connections
 *   recv()   returns -1 immediately if no data available
 *   send()   returns -1 immediately if send buffer full
 *   
 *   Benefit: Single thread can poll multiple sockets
 * 
 * ERROR HANDLING:
 *   If no data/connections: errno = EAGAIN or EWOULDBLOCK (UNIX)
 *                          WSAGetLastError() = WSAEWOULDBLOCK (Windows)
 *   
 *   Application must:
 *     - Check return value and errno
 *     - Retry operation later
 *     - Or use select/poll/epoll for event notification
 * 
 * FCNTL EXPLAINED (UNIX):
 *   fcntl(fd, F_SETFL, flags)
 *   
 *   F_SETFL: Set file status flags
 *   O_NONBLOCK: Non-blocking I/O flag
 *   
 *   Note: This overwrites existing flags (better to F_GETFL first, then OR)
 *   Safe here since socket just created (no other flags set)
 * 
 * IOCTLSOCKET EXPLAINED (Windows):
 *   ioctlsocket(socket, FIONBIO, &mode)
 *   
 *   FIONBIO: File I/O Non-Blocking I/O
 *   mode=1:  Enable non-blocking
 *   mode=0:  Disable non-blocking
 * 
 * STEP 5: BIND TO ADDRESS AND PORT
 * ──────────────────────────────────────────────────────────────────
 * bind(server_fd, (struct sockaddr*)&addr, sizeof(addr))
 * 
 * sockaddr_in structure:
 *   struct sockaddr_in {
 *       sa_family_t    sin_family;  // AF_INET (IPv4)
 *       in_port_t      sin_port;    // Port number (network byte order)
 *       struct in_addr sin_addr;    // IP address (network byte order)
 *       char           sin_zero[8]; // Padding (must be zero)
 *   };
 * 
 * Field initialization:
 *   sin_family = AF_INET
 *     Specifies IPv4 addressing
 *   
 *   sin_addr.s_addr = INADDR_ANY (0.0.0.0)
 *     Bind to all network interfaces
 *     Allows connections from any interface (Ethernet, Wi-Fi, loopback)
 *     
 *     Alternatives:
 *       inet_addr("127.0.0.1")   - Only loopback (localhost)
 *       inet_addr("192.168.1.5") - Specific interface
 *   
 *   sin_port = htons(port)
 *     Port number in network byte order (big-endian)
 *     htons() converts host byte order to network byte order
 * 
 * NETWORK BYTE ORDER (Big-Endian):
 *   TCP/IP standards require big-endian (MSB first)
 *   
 *   htons() - Host TO Network Short (16-bit)
 *     On big-endian machine (SPARC, PowerPC):  No-op
 *     On little-endian machine (x86, ARM):     Swaps bytes
 *   
 *   Example on x86:
 *     port = 43594 = 0xAA3A
 *     htons(43594) = 0x3AAA (bytes swapped)
 *   
 *   Related functions:
 *     htonl() - Host TO Network Long (32-bit)
 *     ntohs() - Network TO Host Short
 *     ntohl() - Network TO Host Long
 * 
 * BIND OPERATION:
 *   Associates socket with IP address and port
 *   Kernel adds entry to socket table
 *   Future packets to this address:port are routed to this socket
 *   
 *   Can fail with:
 *     EADDRINUSE:    Port already in use
 *     EACCES:        Port < 1024 and not root/admin
 *     EADDRNOTAVAIL: Invalid IP address
 * 
 * STEP 6: START LISTENING
 * ──────────────────────────────────────────────────────────────────
 * listen(server_fd, 10)
 * 
 * Purpose: Mark socket as passive (accepting connections)
 * 
 * Backlog parameter (10):
 *   Maximum length of pending connection queue
 *   
 *   When client calls connect():
 *     1. TCP three-way handshake occurs
 *     2. Connection enters queue
 *     3. Waits for server to accept()
 *   
 *   If queue full (10 pending connections):
 *     - New SYN packets are ignored/dropped
 *     - Client connect() times out
 *   
 *   Typical values:
 *     Small servers: 5-10
 *     Web servers:   128-1024
 * 
 * LISTEN QUEUE DIAGRAM:
 * ┌────────────────────────────────────────────────────────────┐
 * │ Server Socket (LISTEN state):                              │
 * │                                                            │
 * │  Pending Connection Queue (max 10):                        │
 * │  ┌─────────┬─────────┬─────────┬─────────┬─────┐           │
 * │  │Client 1 │Client 2 │Client 3 │  ...    │Empty│           │
 * │  └─────────┴─────────┴─────────┴─────────┴─────┘           │
 * │       ↑                                                    │
 * │       accept() removes from queue                          │
 * │                                                            │
 * │  If queue full: New connections dropped                    │
 * └────────────────────────────────────────────────────────────┘
 * 
 * SOCKET STATE TRANSITION:
 *   CLOSED → BOUND (after bind)
 *   BOUND  → LISTEN (after listen)
 *   LISTEN → [stays LISTEN, accept creates new ESTABLISHED socket]
 * 
 * FAILURE MODES:
 *   Returns 0 on success, -1 on error
 *   Errors rare (usually means socket not bound)
 * 
 * ERROR RECOVERY:
 *   On any error in network_init():
 *     1. Close socket if created
 *     2. Call WSACleanup if on Windows
 *     3. Set server_fd = -1
 *     4. Return false
 *   
 *   Ensures no resource leaks on partial initialization
 * 
 * COMPLEXITY: O(1) time, O(1) space
 */
bool network_init(NetworkServer* server, u16 port) {
    /* Store port and initialize state */
    server->port = port;
    server->running = false;

#ifdef _WIN32
    /*
     * WINDOWS: Initialize Winsock library
     * 
     * Winsock is a DLL (ws2_32.dll) that must be loaded before use.
     * This is different from POSIX where sockets are part of the kernel.
     */
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        /* WSAStartup failed - Winsock DLL not available */
        return false;
    }
#endif

    /*
     * STEP 1: Create TCP socket
     * 
     * socket(domain, type, protocol)
     *   domain:   AF_INET (IPv4)
     *   type:     SOCK_STREAM (TCP, connection-oriented)
     *   protocol: 0 (auto-select TCP for SOCK_STREAM)
     * 
     * Returns socket file descriptor (or INVALID_SOCKET on error)
     */
    server->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->server_fd < 0) {
        /* Socket creation failed (out of file descriptors, etc.) */
#ifdef _WIN32
        WSACleanup();  /* Cleanup Winsock on Windows */
#endif
        return false;
    }

    /*
     * STEP 2: Set SO_REUSEADDR socket option
     * 
     * Allows binding to port in TIME_WAIT state (after previous close).
     * Essential for server development (frequent restarts).
     * 
     * Without this: bind() fails with "Address already in use" for ~60 seconds
     * With this:    bind() succeeds immediately
     */
    int opt = 1;  /* 1 = enable option, 0 = disable */
    if (setsockopt(server->server_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt)) < 0) {
        /* setsockopt failed (very rare) */
#ifdef _WIN32
        closesocket(server->server_fd);
        WSACleanup();
#else
        close(server->server_fd);
#endif
        return false;
    }

    /*
     * STEP 3: Set non-blocking mode
     * 
     * Non-blocking mode allows polling multiple clients without threads.
     * accept(), recv(), send() return immediately instead of blocking.
     */
#ifdef _WIN32
    /*
     * Windows: Use ioctlsocket with FIONBIO
     * 
     * u_long mode = 1:  Non-blocking
     * u_long mode = 0:  Blocking
     */
    u_long mode = 1;
    ioctlsocket(server->server_fd, FIONBIO, &mode);
#else
    /*
     * UNIX: Use fcntl with O_NONBLOCK flag
     * 
     * F_SETFL: Set file status flags
     * O_NONBLOCK: Non-blocking I/O
     */
    fcntl(server->server_fd, F_SETFL, O_NONBLOCK);
#endif

    /*
     * STEP 4: Prepare socket address structure
     * 
     * sockaddr_in specifies IP address and port to bind.
     */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));  /* Zero all fields (important for sin_zero padding) */
    
    addr.sin_family = AF_INET;                /* IPv4 */
    addr.sin_addr.s_addr = INADDR_ANY;        /* 0.0.0.0 - bind to all interfaces */
    addr.sin_port = htons(port);              /* Convert port to network byte order */

    /*
     * STEP 5: Bind socket to address and port
     * 
     * Associates socket with IP:port combination.
     * Kernel will route packets to this address to our socket.
     */
    if (bind(server->server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        /* bind() failed - port in use, permission denied, etc. */
#ifdef _WIN32
        closesocket(server->server_fd);
        WSACleanup();
#else
        close(server->server_fd);
#endif
        return false;
    }

    /*
     * STEP 6: Start listening for connections
     * 
     * Marks socket as passive (accepting connections).
     * Creates queue for pending connections (backlog=10).
     */
    if (listen(server->server_fd, 10) < 0) {
        /* listen() failed (socket not bound, etc.) */
#ifdef _WIN32
        closesocket(server->server_fd);
        WSACleanup();
#else
        close(server->server_fd);
#endif
        return false;
    }

    /*
     * Success: Server is ready to accept connections
     */
    server->running = true;
    return true;
}

/*******************************************************************************
 * SERVER SHUTDOWN
 ******************************************************************************/

/*
 * network_shutdown - Close server socket and cleanup resources
 * 
 * @param server  Pointer to initialized NetworkServer
 * 
 * ALGORITHM:
 *   1. Close listening socket (if valid)
 *   2. [Windows only] Cleanup Winsock library
 *   3. Reset server state
 * 
 * SOCKET CLOSURE:
 *   close()/closesocket() performs graceful shutdown:
 *     1. Sends FIN packet to connected clients
 *     2. Waits for ACKs (may take up to 60 seconds in TIME_WAIT)
 *     3. Frees kernel resources (socket buffers, control blocks)
 *     4. Releases port binding
 * 
 * IMPORTANT: This only closes the LISTENING socket
 *   Client sockets (from accept) must be closed separately
 *   
 *   Proper shutdown order:
 *     for (each client) {
 *         network_close_socket(client_fd);
 *     }
 *     network_shutdown(&server);
 * 
 * WSACLEANUP (Windows):
 *   Winsock maintains reference count:
 *     - WSAStartup() increments count
 *     - WSACleanup() decrements count
 *     - When count reaches 0, Winsock DLL is unloaded
 *   
 *   Must balance each WSAStartup() with WSACleanup()
 * 
 * COMPLEXITY: O(1) time
 */
void network_shutdown(NetworkServer* server) {
    /* Close listening socket if valid */
    if (server->server_fd >= 0) {
#ifdef _WIN32
        closesocket(server->server_fd);  /* Windows: closesocket() */
#else
        close(server->server_fd);        /* UNIX: close() */
#endif
        server->server_fd = -1;  /* Mark as invalid */
    }
    
    /* Mark server as stopped */
    server->running = false;
    
#ifdef _WIN32
    /*
     * Windows: Cleanup Winsock library
     * Decrements reference count, unloads DLL if zero
     */
    WSACleanup();
#endif
}

/*******************************************************************************
 * CONNECTION MANAGEMENT
 ******************************************************************************/

/*
 * network_accept_connection - Accept pending client connection (non-blocking)
 * 
 * @param server  Pointer to initialized NetworkServer
 * @return        Client socket fd (>= 0) on success, -1 if no pending connections
 * 
 * ALGORITHM:
 *   1. Call accept() to extract first queued connection
 *   2. If successful, set new client socket to non-blocking mode
 *   3. Return client socket fd
 * 
 * ACCEPT OPERATION:
 *   accept(sockfd, addr, addrlen)
 *   
 *   Parameters:
 *     sockfd:  Server listening socket
 *     addr:    Output parameter - receives client address info
 *     addrlen: Input/output parameter - size of addr structure
 *   
 *   Returns:
 *     >= 0: New socket for client communication
 *     -1:   No pending connections (non-blocking) or error
 * 
 * CLIENT ADDRESS STRUCTURE:
 *   struct sockaddr_in client_addr;
 *   After accept(), contains:
 *     - sin_addr.s_addr: Client IP address (e.g., 192.168.1.100)
 *     - sin_port:        Client source port (ephemeral, 49152-65535)
 *     - sin_family:      AF_INET
 * 
 * HOW ACCEPT WORKS:
 * 
 * Server Socket State:
 * ┌─────────────────────────────────────────────────────────────┐
 * │ LISTEN Socket (server_fd):                                  │
 * │                                                             │
 * │  Pending Connection Queue:                                  │
 * │  ┌────────────┬────────────┬────────────┬────────┐          │
 * │  │ Client 1   │ Client 2   │ Client 3   │  ...   │          │
 * │  │ 192.168.1.5│ 10.0.0.100 │ 127.0.0.1  │        │          │
 * │  └────────────┴────────────┴────────────┴────────┘          │
 * │       ↑                                                     │
 * │       │                                                     │
 * │  accept() removes first pending connection                  │
 * │                                                             │
 * └─────────────────────────────────────────────────────────────┘
 *                        ↓
 *              Returns NEW socket
 *                        ↓
 * ┌─────────────────────────────────────────────────────────────┐
 * │ ESTABLISHED Socket (client_fd):                             │
 * │                                                             │
 * │  Connected to: 192.168.1.5:49152                            │
 * │  Can recv/send data                                         │
 * │  Independent of server socket                               │
 * └─────────────────────────────────────────────────────────────┘
 * 
 * NON-BLOCKING BEHAVIOR:
 *   Server socket is non-blocking (set in network_init)
 *   
 *   If connection pending:
 *     - accept() returns new client socket immediately
 *     - Queue length decreases by 1
 *   
 *   If queue empty:
 *     - accept() returns -1 immediately (doesn't wait)
 *     - errno = EAGAIN or EWOULDBLOCK (UNIX)
 *     - WSAGetLastError() = WSAEWOULDBLOCK (Windows)
 * 
 * POLLING PATTERN:
 *   while (true) {
 *       i32 client = network_accept_connection(&server);
 *       if (client >= 0) {
 *           // New client connected
 *           add_to_client_list(client);
 *       } else {
 *           // No more pending connections
 *           break;
 *       }
 *   }
 * 
 * CLIENT SOCKET NON-BLOCKING MODE:
 *   Newly accepted socket may or may not inherit non-blocking mode
 *   POSIX doesn't guarantee inheritance, so we explicitly set it
 *   
 *   UNIX:    fcntl(client_fd, F_SETFL, O_NONBLOCK)
 *   Windows: ioctlsocket(client_fd, FIONBIO, &mode) where mode=1
 * 
 * WHY NON-BLOCKING FOR CLIENT SOCKETS?
 *   Allows polling multiple clients without blocking:
 *   
 *   for (each client) {
 *       i32 n = recv(client_fd, buf, 512);
 *       if (n > 0) {
 *           // Process data
 *       } else if (n < 0 && errno == EAGAIN) {
 *           // No data yet, check next client
 *       }
 *   }
 * 
 * SOCKET LIFECYCLE:
 *   1. Client calls connect()
 *   2. TCP three-way handshake occurs
 *   3. Connection enters server's pending queue
 *   4. Server calls accept() → returns new socket
 *   5. Server can recv/send on new socket
 *   6. Either side calls close() → TCP four-way teardown
 * 
 * SECURITY NOTE:
 *   accept() has no authentication - it accepts ALL connections
 *   Application must validate clients after accept:
 *     - Check IP address whitelist/blacklist
 *     - Require login credentials
 *     - Rate limit connections per IP
 * 
 * COMPLEXITY: O(1) time
 */
i32 network_accept_connection(NetworkServer* server) {
    /*
     * Prepare client address structure (filled by accept)
     */
    struct sockaddr_in client_addr;
    
#ifdef _WIN32
    int client_len = sizeof(client_addr);  /* Windows uses int */
#else
    socklen_t client_len = sizeof(client_addr);  /* POSIX uses socklen_t */
#endif

    /*
     * Accept first pending connection from queue
     * 
     * Non-blocking: Returns immediately even if no connections pending
     */
    i32 client_fd = accept(server->server_fd, (struct sockaddr*)&client_addr, &client_len);
    
    if (client_fd >= 0) {
        /*
         * Connection accepted successfully
         * Set new client socket to non-blocking mode
         */
#ifdef _WIN32
        u_long mode = 1;  /* 1 = non-blocking */
        ioctlsocket(client_fd, FIONBIO, &mode);
#else
        fcntl(client_fd, F_SETFL, O_NONBLOCK);
#endif
    }
    /* else: No pending connection (EAGAIN) or error */

    return client_fd;
}

/*******************************************************************************
 * DATA TRANSFER
 ******************************************************************************/

/*
 * network_receive - Read bytes from client socket (non-blocking)
 * 
 * @param socket_fd  Client socket file descriptor
 * @param buffer     Destination buffer for received data
 * @param length     Maximum bytes to read
 * @return           Bytes received (>0), 0 on disconnect, -1 on error/no data
 * 
 * ALGORITHM:
 *   Call recv() and return result directly
 * 
 * RECV OPERATION:
 *   recv(sockfd, buf, len, flags)
 *   
 *   Flags = 0 (standard receive):
 *     - Read and remove data from receive buffer
 *     - May return fewer bytes than requested
 *     - Returns 0 if peer closed connection
 *     - Returns -1 if error or no data (non-blocking)
 * 
 * DATA FLOW:
 * ┌──────────────────────────────────────────────────────────────┐
 * │ Remote Client:                                               │
 * │   send(fd, data, 100)                                        │
 * │          ↓                                                   │
 * └──────────┼───────────────────────────────────────────────────┘
 *            │ Network (TCP packets)
 *            ↓
 * ┌──────────▼──────────────────────────────────────────────────┐
 * │ KERNEL (Operating System):                                  │
 * │                                                             │
 * │  TCP Receive Buffer:                                        │
 * │  ┌───────────────────────────────────────────┐              │
 * │  │ [Data from network] (up to 64 KB)         │              │
 * │  └───────────────────────────────────────────┘              │
 * │                                                             │
 * │  recv() copies data from kernel buffer to user buffer       │
 * │                                                             │
 * └──────────┬──────────────────────────────────────────────────┘
 *            │ memcpy to user space
 *            ↓
 * ┌──────────▼──────────────────────────────────────────────────┐
 * │ APPLICATION (User Space):                                   │
 * │                                                             │
 * │  u8 buffer[512];                                            │
 * │  i32 n = network_receive(fd, buffer, 512);                  │
 * │                                                             │
 * │  buffer now contains received data (n bytes)                │
 * └─────────────────────────────────────────────────────────────┘
 * 
 * RETURN VALUE CASES:
 * 
 * 1. bytes > 0: Data received
 *    buffer[0..bytes-1] contains valid data
 *    May be less than 'length' requested
 *    More data may be waiting in kernel buffer
 * 
 * 2. bytes == 0: Graceful disconnect
 *    Client called close() or shutdown()
 *    TCP FIN packet received
 *    No more data will arrive
 *    Action: Close socket and remove client
 * 
 * 3. bytes == -1 && errno == EAGAIN: No data available
 *    Non-blocking socket, kernel buffer empty
 *    NOT an error - normal condition
 *    Action: Try again later
 * 
 * 4. bytes == -1 && errno != EAGAIN: Error
 *    ECONNRESET: Connection reset by peer (client crashed)
 *    ETIMEDOUT:  Connection timed out
 *    ENOTCONN:   Socket not connected
 *    Action: Close socket and remove client
 * 
 * TCP STREAM SEMANTICS:
 *   TCP provides BYTE STREAM, not message boundaries
 *   
 *   Sender:   send(10 bytes); send(20 bytes);
 *   Receiver: May get recv(30 bytes) - combined
 *             Or recv(15), recv(15) - split differently
 *   
 *   Application must implement framing:
 *     - Length prefix: [2-byte length][payload]
 *     - Delimiter:     [data][newline]
 *     - Fixed size:    Always 100 bytes per message
 * 
 * EXAMPLE: Reading variable-length packet
 *   // Read 2-byte length header
 *   u8 header[2];
 *   i32 n = network_receive(fd, header, 2);
 *   if (n == 2) {
 *       u16 len = (header[0] << 8) | header[1];
 *       
 *       // Read payload
 *       u8* payload = malloc(len);
 *       i32 total = 0;
 *       while (total < len) {
 *           i32 m = network_receive(fd, payload + total, len - total);
 *           if (m > 0) total += m;
 *           else if (m == 0 || (m < 0 && errno != EAGAIN)) break;  // Error
 *       }
 *   }
 * 
 * BUFFER OVERFLOW PREVENTION:
 *   CRITICAL: Never recv() more than buffer can hold
 *   
 *   u8 buf[512];
 *   network_receive(fd, buf, 512);   ✓ Safe
 *   network_receive(fd, buf, 1024);  ✗ BUFFER OVERFLOW!
 * 
 * PLATFORM DIFFERENCES:
 *   POSIX (Linux/macOS):
 *     - recv() returns ssize_t (signed)
 *     - Error code in global 'errno'
 *   
 *   Windows (Winsock):
 *     - recv() returns int
 *     - Error code via WSAGetLastError()
 *     - WSAEWOULDBLOCK instead of EAGAIN
 * 
 * COMPLEXITY: O(1) to O(length) time
 */
i32 network_receive(i32 socket_fd, u8* buffer, u32 length) {
    /*
     * recv() reads data from kernel TCP receive buffer into user buffer
     * 
     * Parameters:
     *   socket_fd: Connected client socket
     *   buffer:    Destination for received data
     *   length:    Maximum bytes to read
     *   flags=0:   Standard receive (read and remove data)
     * 
     * Cast to (char*) for Windows compatibility (Winsock expects char*)
     */
    return recv(socket_fd, (char*)buffer, length, 0);
}

/*
 * network_send - Write bytes to client socket (non-blocking)
 * 
 * @param socket_fd  Client socket file descriptor
 * @param buffer     Source buffer containing data to send
 * @param length     Number of bytes to write
 * @return           Bytes sent (>0), 0 if nothing sent, -1 on error
 * 
 * ALGORITHM:
 *   1. Dump transmitted data as hex (for debugging)
 *   2. Call send() and return result
 * 
 * SEND OPERATION:
 *   send(sockfd, buf, len, flags)
 *   
 *   Flags = 0 (standard send):
 *     - Copy data to kernel send buffer
 *     - May send fewer bytes than requested (if buffer full)
 *     - Returns number of bytes queued
 *     - Returns -1 on error
 * 
 * DATA FLOW:
 * ┌──────────────────────────────────────────────────────────────┐
 * │ APPLICATION (User Space):                                    │
 * │                                                              │
 * │  u8 packet[100] = {...};                                     │
 * │  network_send(fd, packet, 100);                              │
 * │          ↓                                                   │
 * └──────────┼───────────────────────────────────────────────────┘
 *            │ memcpy to kernel space
 *            ↓
 * ┌──────────▼──────────────────────────────────────────────────┐
 * │ KERNEL (Operating System):                                  │
 * │                                                             │
 * │  TCP Send Buffer:                                           │
 * │  ┌───────────────────────────────────────────┐              │
 * │  │ [Data queued for transmission] (16-64 KB) │              │
 * │  └───────────────────────────────────────────┘              │
 * │                                                             │
 * │  TCP segments data into packets (MSS=1460 bytes)            │
 * │  Adds headers, checksums, sequence numbers                  │
 * │  Transmits packets, waits for ACKs, retransmits if needed   │
 * │                                                             │
 * └──────────┬──────────────────────────────────────────────────┘
 *            │ Network (TCP packets)
 *            ↓
 * ┌──────────▼──────────────────────────────────────────────────┐
 * │ Remote Client:                                              │
 * │   recv(fd, buffer, 512)                                     │
 * │   Receives data                                             │
 * └─────────────────────────────────────────────────────────────┘
 * 
 * RETURN VALUE CASES:
 * 
 * 1. bytes == length: Full write
 *    All data successfully queued in kernel buffer
 *    TCP will transmit and retransmit as needed
 * 
 * 2. 0 < bytes < length: Partial write
 *    Kernel send buffer is full (or nearly full)
 *    Only 'bytes' were queued, rest rejected
 *    Application must retry sending remaining data
 *    
 *    Example:
 *      u8 packet[1000];
 *      i32 sent = 0;
 *      while (sent < 1000) {
 *          i32 n = network_send(fd, packet + sent, 1000 - sent);
 *          if (n > 0) {
 *              sent += n;
 *          } else if (n < 0 && errno != EAGAIN) {
 *              // Error, abort
 *              break;
 *          }
 *          // If EAGAIN, retry later
 *      }
 * 
 * 3. bytes == -1 && errno == EAGAIN: Buffer full
 *    Non-blocking socket, kernel send buffer is full
 *    NOT an error - receiver is slow or network congested
 *    Action: Retry later or use select/poll for writability
 * 
 * 4. bytes == -1 && errno != EAGAIN: Error
 *    EPIPE:       Connection closed by peer (broken pipe)
 *    ECONNRESET:  Connection reset
 *    ENOTCONN:    Socket not connected
 *    Action: Close socket and remove client
 * 
 * TCP SEND BUFFER:
 *   Kernel maintains send buffer (typically 16-64 KB)
 *   
 *   Data in buffer is:
 *     - Segmented into packets (MSS = 1460 bytes for Ethernet)
 *     - Transmitted over network
 *     - Kept until ACK received (for retransmission)
 *     - Removed when ACK confirms delivery
 *   
 *   If buffer fills up:
 *     - send() returns partial write or EAGAIN
 *     - Application must wait or buffer data
 * 
 * FLOW CONTROL:
 *   Receiver advertises window size (available buffer space)
 *   Sender limits unacknowledged data to window size
 *   
 *   If receiver window = 0:
 *     - Sender stops transmitting
 *     - send() may return EAGAIN
 *     - Waits for window update from receiver
 * 
 * HEX DUMP (Debug Feature):
 *   Before sending, this function dumps bytes as hex:
 *   
 *   [HEX] TX len=10: 01 02 03 04 05 06 07 08 09 0A
 *   
 *   Benefits:
 *     - Verify packet structure matches protocol spec
 *     - Debug encoding issues (endianness, padding)
 *     - Compare with Wireshark captures
 *   
 *   Example output:
 *     [HEX] TX len=5: B8 00 04 30 39
 *              ^       ^  ^  ^  ^  ^
 *              |       |  |  |  |  └─ Payload byte 2
 *              |       |  |  |  └──── Payload byte 1
 *              |       |  |  └─────── Length MSB
 *              |       |  └────────── Length LSB
 *              |       └───────────── Encrypted opcode
 *              └───────────────────── Tag (TX = transmit)
 * 
 * PLATFORM DIFFERENCES:
 *   POSIX:   send() returns ssize_t
 *   Windows: send() returns int, expects (const char*) buffer
 * 
 * COMPLEXITY: O(1) to O(length) time
 */
i32 network_send(i32 socket_fd, const u8* buffer, u32 length) {
    /*
     * Debug: Dump transmitted bytes as hex
     * Helps verify packet structure during development
     */
    dump_hex("TX", buffer, length);

    /*
     * send() copies data to kernel TCP send buffer
     * 
     * Parameters:
     *   socket_fd: Connected client socket
     *   buffer:    Source data to transmit
     *   length:    Number of bytes to send
     *   flags=0:   Standard send
     * 
     * Cast to (const char*) for Windows compatibility
     */
    return send(socket_fd, (const char*)buffer, length, 0);
}

/*******************************************************************************
 * SOCKET CLEANUP
 ******************************************************************************/

/*
 * network_close_socket - Close client socket and free resources
 * 
 * @param socket_fd  Socket file descriptor to close
 * 
 * ALGORITHM:
 *   1. Validate socket_fd (>= 0)
 *   2. Call close() or closesocket()
 *   3. OS performs TCP teardown
 * 
 * WHAT HAPPENS WHEN CLOSING A SOCKET:
 * 
 * 1. Application calls close()/closesocket()
 * 2. TCP initiates graceful shutdown (four-way handshake):
 *    
 *    Local                               Remote
 *      │                                    │
 *      │────── FIN (seq=x) ────────────────>│  "I'm done sending"
 *      │                                    │
 *      │<───── ACK (ack=x+1) ───────────────│  "I received your FIN"
 *      │                                    │
 *      │<───── FIN (seq=y) ─────────────────│  "I'm done too"
 *      │                                    │
 *      │────── ACK (ack=y+1) ──────────────>│  "Acknowledged"
 *      │                                    │
 *    CLOSED                              CLOSED
 * 
 * 3. Socket enters TIME_WAIT state (typically 60 seconds)
 *    - Ensures all packets from old connection have expired
 *    - Prevents delayed packets from confusing new connections
 *    - Port cannot be reused during TIME_WAIT (unless SO_REUSEADDR set)
 * 
 * 4. Kernel frees resources:
 *    - File descriptor table entry
 *    - TCP send/receive buffers (64-128 KB)
 *    - TCP control block (sequence numbers, timers, etc.)
 *    - Routing cache entry
 * 
 * GRACEFUL VS ABORTIVE CLOSE:
 * 
 * GRACEFUL (this function):
 *   - close()/closesocket()
 *   - Sends FIN packet
 *   - Waits for peer acknowledgment
 *   - Ensures all data delivered
 *   - Takes up to 60 seconds (TIME_WAIT)
 * 
 * ABORTIVE:
 *   - setsockopt(SOL_SOCKET, SO_LINGER, {1, 0})
 *   - Sends RST packet (reset)
 *   - Immediately closes connection
 *   - Discards unsent data
 *   - No TIME_WAIT state
 *   
 *   Used when:
 *     - Client is misbehaving (flood attack)
 *     - Protocol violation detected
 *     - Immediate shutdown required
 * 
 * WHEN TO CLOSE CLIENT SOCKETS:
 *   - Client disconnects (recv returns 0)
 *   - Protocol error (invalid packet)
 *   - Timeout (no activity for N seconds)
 *   - Server shutdown
 *   - Authentication failure
 * 
 * PLATFORM DIFFERENCES:
 *   POSIX (Linux/macOS):
 *     close(fd)
 *     - Works for files, sockets, pipes, etc.
 *     - Returns 0 on success, -1 on error
 *   
 *   Windows (Winsock):
 *     closesocket(fd)
 *     - Only for sockets (use CloseHandle for files)
 *     - Returns 0 on success, SOCKET_ERROR on error
 * 
 * ERROR HANDLING:
 *   Errors on close are rare and usually ignored:
 *     - EBADF: Invalid file descriptor (already closed)
 *     - EINTR: Interrupted by signal
 *   
 *   Safe to ignore because:
 *     - Socket is being destroyed anyway
 *     - Can't retry close() (fd already released)
 * 
 * COMPLEXITY: O(1) time
 */
void network_close_socket(i32 socket_fd) {
    /* Only close if socket is valid (>= 0) */
    if (socket_fd >= 0) {
#ifdef _WIN32
        /*
         * Windows: Use closesocket()
         * Do NOT use CloseHandle() for sockets
         */
        closesocket(socket_fd);
#else
        /*
         * UNIX: Use close()
         * Same function for files, sockets, pipes, etc.
         */
        close(socket_fd);
#endif
    }
}
