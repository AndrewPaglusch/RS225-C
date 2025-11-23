/*******************************************************************************
 * NETWORK.H - TCP/IP Socket Server for RuneScape Protocol
 *******************************************************************************
 * 
 * EDUCATIONAL PURPOSE:
 * This file demonstrates fundamental concepts in:
 *   - TCP/IP networking (connection-oriented communication)
 *   - Berkeley Sockets API (POSIX standard for network I/O)
 *   - Non-blocking I/O (polling multiple clients without threads)
 *   - Cross-platform socket programming (Windows vs POSIX)
 *   - Socket lifecycle management (socket -> bind -> listen -> accept)
 *   - Network byte order (endianness in protocol headers)
 *   - Error handling in network operations
 * 
 * CORE CONCEPT - TCP/IP NETWORKING:
 * 
 * TCP (Transmission Control Protocol) provides:
 *   - Reliable delivery (automatic retransmission of lost packets)
 *   - In-order delivery (bytes arrive in same sequence as sent)
 *   - Connection-oriented (explicit setup/teardown handshake)
 *   - Flow control (prevents sender from overwhelming receiver)
 *   - Error detection (checksums validate data integrity)
 * 
 * IP (Internet Protocol) provides:
 *   - Addressing (IPv4: 32-bit, IPv6: 128-bit)
 *   - Routing (packet forwarding across networks)
 *   - Fragmentation (splitting large packets for transmission)
 * 
 * NETWORK STACK LAYERS (OSI Model):
 * ┌──────────────────────────────────────────────────────────────────────┐
 * │ Layer 7: Application (HTTP, FTP, RuneScape protocol)                 │
 * ├──────────────────────────────────────────────────────────────────────┤
 * │ Layer 6: Presentation (SSL/TLS encryption, data encoding)            │
 * ├──────────────────────────────────────────────────────────────────────┤
 * │ Layer 5: Session (connection management, authentication)             │
 * ├──────────────────────────────────────────────────────────────────────┤
 * │ Layer 4: Transport (TCP, UDP) ← Berkeley Sockets operate here        │
 * ├──────────────────────────────────────────────────────────────────────┤
 * │ Layer 3: Network (IP routing, addressing)                            │
 * ├──────────────────────────────────────────────────────────────────────┤
 * │ Layer 2: Data Link (Ethernet, Wi-Fi MAC addresses)                   │
 * ├──────────────────────────────────────────────────────────────────────┤
 * │ Layer 1: Physical (electrical signals, cables, radio waves)          │
 * └──────────────────────────────────────────────────────────────────────┘
 * 
 * BERKELEY SOCKETS API:
 * 
 * The Berkeley Sockets API (BSD Sockets) is the de facto standard for
 * network programming. Originated in 4.2BSD Unix (1983), now used by:
 *   - POSIX systems (Linux, macOS, BSD, Solaris)
 *   - Windows (Winsock - Windows Sockets with minor differences)
 *   - Mobile platforms (Android, iOS)
 * 
 * SOCKET LIFECYCLE (Server):
 * ┌──────────────────────────────────────────────────────────────────────┐
 * │  1. socket()   - Create socket file descriptor                       │
 * │                  Returns: Integer handle (UNIX: 3+, Windows: SOCKET) │
 * │                                                                      │
 * │  2. bind()     - Associate socket with IP address and port           │
 * │                  Example: 0.0.0.0:43594 (all interfaces, port 43594) │
 * │                                                                      │
 * │  3. listen()   - Mark socket as passive (accepting connections)      │
 * │                  Backlog queue holds pending connections             │
 * │                                                                      │
 * │  4. accept()   - Extract first queued connection (blocking/polling)  │
 * │                  Returns: New socket for client communication        │
 * │                                                                      │
 * │  5. recv()     - Read bytes from client socket                       │
 * │     send()     - Write bytes to client socket                        │
 * │                                                                      │
 * │  6. close()    - Shutdown connection and release resources           │
 * │                  (closesocket on Windows)                            │
 * └──────────────────────────────────────────────────────────────────────┘
 * 
 * TCP THREE-WAY HANDSHAKE:
 * 
 * Establishing a TCP connection requires three packets:
 * 
 *    Client                           Server
 *      │                                │
 *      │─────── SYN (seq=x) ───────────>│  1. Client sends SYN
 *      │                                │     (synchronize sequence number)
 *      │                                │
 *      │<──── SYN-ACK (seq=y,ack=x+1) ──│  2. Server responds SYN-ACK
 *      │                                │     (acknowledge + own SYN)
 *      │                                │
 *      │─────── ACK (ack=y+1) ─────────>│  3. Client acknowledges
 *      │                                │
 *      │═══════ Connection Open ═══════ │
 *      │                                │
 * 
 * WHY THREE PACKETS?
 *   - Ensures both sides agree on initial sequence numbers
 *   - Prevents old duplicate packets from confusing new connections
 *   - Allows both sides to allocate resources before data transfer
 * 
 * CONNECTION TERMINATION (Four-Way Handshake):
 * 
 *    Client                           Server
 *      │                                │
 *      │─────── FIN (seq=x) ───────────>│  1. Client closes send
 *      │                                │
 *      │<────── ACK (ack=x+1) ──────────│  2. Server acknowledges
 *      │                                │
 *      │<────── FIN (seq=y) ────────────│  3. Server closes send
 *      │                                │
 *      │─────── ACK (ack=y+1) ─────────>│  4. Client acknowledges
 *      │                                │
 *      │      Connection Closed         │
 * 
 * NON-BLOCKING I/O:
 * 
 * BLOCKING MODE (default):
 *   - recv() waits until data arrives (thread sleeps)
 *   - accept() waits for incoming connection
 *   - Problem: One slow client blocks entire server
 * 
 * NON-BLOCKING MODE:
 *   - recv() returns immediately with EAGAIN/EWOULDBLOCK if no data
 *   - accept() returns immediately if no pending connections
 *   - Allows single-threaded server to handle multiple clients
 *   - Requires polling loop or event notification (select/poll/epoll)
 * 
 * FILE DESCRIPTORS (UNIX):
 * 
 * In UNIX, "everything is a file":
 *   - Regular files: fd=open("file.txt")
 *   - Sockets:       fd=socket(AF_INET, SOCK_STREAM, 0)
 *   - Pipes:         fd=pipe(fds)
 *   - Devices:       fd=open("/dev/null")
 * 
 * STANDARD FILE DESCRIPTORS:
 *   0 = stdin  (standard input)
 *   1 = stdout (standard output)
 *   2 = stderr (standard error)
 *   3+ = user-created file descriptors (including sockets)
 * 
 * OPERATIONS: read(), write(), close() work on all file descriptor types
 * 
 * WINDOWS SOCKETS (Winsock):
 * 
 * Windows uses SOCKET type (opaque handle) instead of integer fd:
 *   - SOCKET is typedef'd to UINT_PTR (pointer-sized unsigned integer)
 *   - INVALID_SOCKET constant instead of -1
 *   - Must initialize Winsock via WSAStartup() before use
 *   - Must cleanup via WSACleanup() before exit
 *   - Uses closesocket() instead of close()
 *   - Uses ioctlsocket() instead of fcntl() for non-blocking mode
 * 
 * PLATFORM DIFFERENCES SUMMARY:
 * ┌──────────────────────┬─────────────────────┬─────────────────────┐
 * │ Operation            │ POSIX (Linux/Mac)   │ Windows (Winsock)   │
 * ├──────────────────────┼─────────────────────┼─────────────────────┤
 * │ Socket type          │ int                 │ SOCKET              │
 * │ Invalid value        │ -1                  │ INVALID_SOCKET      │
 * │ Initialization       │ (none)              │ WSAStartup()        │
 * │ Cleanup              │ (none)              │ WSACleanup()        │
 * │ Close socket         │ close(fd)           │ closesocket(s)      │
 * │ Non-blocking mode    │ fcntl(F_SETFL)      │ ioctlsocket(FIONBIO)│
 * │ Error code           │ errno               │ WSAGetLastError()   │
 * │ Cast for sockopt     │ (void*)             │ (char*)             │
 * └──────────────────────┴─────────────────────┴─────────────────────┘
 * 
 * SOCKET OPTIONS:
 * 
 * SO_REUSEADDR (used in this code):
 *   - Allows binding to recently-used port (in TIME_WAIT state)
 *   - Without this: "Address already in use" error after server restart
 *   - TIME_WAIT state: TCP keeps closed ports reserved for ~60 seconds
 *   - Essential for development (frequent server restarts)
 * 
 * TCP_NODELAY (optional, not used here):
 *   - Disables Nagle's algorithm (batches small packets)
 *   - Reduces latency for small, frequent messages
 *   - Increases bandwidth usage (more packet headers)
 *   - Useful for interactive games
 * 
 * ERROR CODES:
 * 
 * UNIX errno values:
 *   EAGAIN / EWOULDBLOCK  - No data available (non-blocking socket)
 *   EINTR                 - System call interrupted by signal
 *   ECONNRESET            - Connection reset by peer
 *   EPIPE                 - Broken pipe (writing to closed socket)
 *   EADDRINUSE            - Address already in use
 *   EADDRNOTAVAIL         - Address not available
 * 
 * Windows WSAGetLastError() values:
 *   WSAEWOULDBLOCK        - Non-blocking operation would block
 *   WSAECONNRESET         - Connection reset by peer
 *   WSAEADDRINUSE         - Address already in use
 *   WSAENETDOWN           - Network subsystem failure
 * 
 * USAGE PATTERN (RuneScape Server):
 * 
 * 1. Initialize server:
 *    NetworkServer server;
 *    network_init(&server, 43594);
 * 
 * 2. Main game loop:
 *    while (server.running) {
 *        // Non-blocking accept (returns -1 if no pending connections)
 *        i32 client_fd = network_accept_connection(&server);
 *        if (client_fd >= 0) {
 *            // New client connected
 *            add_client(client_fd);
 *        }
 *        
 *        // Non-blocking receive from all clients
 *        for (each client) {
 *            i32 bytes = network_receive(client_fd, buffer, 512);
 *            if (bytes > 0) {
 *                // Process packet
 *            } else if (bytes == 0) {
 *                // Client disconnected gracefully
 *            } else if (errno != EAGAIN) {
 *                // Error occurred
 *            }
 *        }
 *        
 *        // Update game state
 *        // Send updates to clients
 *    }
 * 
 * 3. Cleanup:
 *    network_shutdown(&server);
 * 
 ******************************************************************************/

#ifndef NETWORK_H
#define NETWORK_H

#include "types.h"

/*******************************************************************************
 * NETWORKSERVER - TCP Server State
 *******************************************************************************
 * 
 * FIELDS:
 *   server_fd:  Listening socket file descriptor
 *               UNIX: Integer >= 0 (typically 3+ since 0-2 are std streams)
 *               Windows: SOCKET handle (opaque pointer-sized value)
 *               Invalid: -1 (UNIX) or INVALID_SOCKET (Windows)
 * 
 *   port:       TCP port number (0-65535)
 *               Well-known ports: 0-1023 (require root/admin)
 *               Registered ports: 1024-49151 (assigned by IANA)
 *               Dynamic ports: 49152-65535 (ephemeral, OS-assigned)
 *               RuneScape default: 43594
 * 
 *   running:    Server state flag
 *               true: Accepting connections
 *               false: Shutting down or not initialized
 * 
 * MEMORY LAYOUT:
 * ┌────────────────────┬────────────────────┬────────────────────┐
 * │ i32 server_fd      │ u16 port           │ bool running       │
 * │ (4 bytes)          │ (2 bytes)          │ (1 byte + 1 pad)   │
 * └────────────────────┴────────────────────┴────────────────────┘
 * Total: 8 bytes (with padding for alignment)
 * 
 * INVARIANTS:
 *   - If running == true, then server_fd >= 0
 *   - If running == false, server_fd may be -1 (uninitialized)
 *   - port is typically constant after initialization
 * 
 * COMPLEXITY:
 *   - Space: O(1) - fixed-size structure
 ******************************************************************************/
typedef struct {
    i32  server_fd;  /* Listening socket file descriptor */
    u16  port;       /* TCP port number (e.g., 43594) */
    bool running;    /* true if server is active */
} NetworkServer;

/*******************************************************************************
 * LIFECYCLE MANAGEMENT
 ******************************************************************************/

/*
 * network_init - Initialize TCP server socket
 * 
 * @param server  Pointer to NetworkServer structure to initialize
 * @param port    TCP port to bind (0-65535, typically 43594 for RuneScape)
 * @return        true on success, false on failure
 * 
 * ALGORITHM:
 *   1. Initialize Winsock on Windows (WSAStartup)
 *   2. Create TCP socket using socket(AF_INET, SOCK_STREAM, 0)
 *   3. Set SO_REUSEADDR option (allows immediate port reuse)
 *   4. Set non-blocking mode (fcntl/ioctlsocket)
 *   5. Bind socket to INADDR_ANY:port (all network interfaces)
 *   6. Start listening for connections (backlog=10)
 * 
 * SOCKET CREATION PARAMETERS:
 *   AF_INET:      Address Family Internet (IPv4)
 *                 AF_INET6 would be for IPv6
 *   
 *   SOCK_STREAM:  Connection-oriented, reliable, byte stream (TCP)
 *                 SOCK_DGRAM would be for connectionless datagrams (UDP)
 *   
 *   Protocol 0:   Automatically select protocol for family+type (TCP)
 * 
 * BINDING DIAGRAM:
 * ┌──────────────────────────────────────────────────────────────────┐
 * │ Network Interfaces:                                              │
 * │                                                                  │
 * │  eth0:    192.168.1.350  (local network)                         │
 * │  wlan0:   10.0.0.50      (Wi-Fi)                                 │
 * │  lo:      127.0.0.1      (loopback)                              │
 * │                                                                  │
 * │  INADDR_ANY (0.0.0.0) → Bind to ALL interfaces                   │
 * │                                                                  │
 * │  Clients can connect via:                                        │
 * │    192.168.1.350:43594                                           │
 * │    10.0.0.50:43594                                               │
 * │    127.0.0.1:43594                                               │
 * └──────────────────────────────────────────────────────────────────┘
 * 
 * PORT NUMBER CONSIDERATIONS:
 *   - Ports 0-1023: Require superuser privileges (avoid)
 *   - Port 43594: RuneScape default (outside privileged range)
 *   - Port must be available (not in use by other processes)
 * 
 * NON-BLOCKING MODE:
 *   UNIX:    fcntl(fd, F_SETFL, O_NONBLOCK)
 *   Windows: ioctlsocket(fd, FIONBIO, &mode) where mode=1
 *   
 *   Effect: accept(), recv(), send() return immediately instead of waiting
 *   
 *   Non-blocking accept():
 *     - Returns client fd if connection pending
 *     - Returns -1 with EAGAIN/EWOULDBLOCK if no connections
 *   
 *   Non-blocking recv():
 *     - Returns byte count if data available
 *     - Returns -1 with EAGAIN/EWOULDBLOCK if no data
 *     - Allows polling multiple clients in single thread
 * 
 * LISTEN BACKLOG:
 *   listen(fd, 10) creates queue for 10 pending connections
 *   
 *   If backlog full:
 *     - New SYN packets are ignored
 *     - Client sees connection timeout
 *     - Must call accept() to drain queue
 *   
 *   Backlog=10 suitable for small game servers
 *   High-traffic servers use larger values (128+)
 * 
 * FAILURE MODES:
 *   - WSAStartup fails (Windows only): Winsock DLL not available
 *   - socket() fails: Out of file descriptors, no network support
 *   - setsockopt() fails: Invalid option (unlikely)
 *   - bind() fails: Port in use, permission denied, invalid address
 *   - listen() fails: Socket not bound (shouldn't happen)
 * 
 * NETWORK BYTE ORDER:
 *   htons() = Host TO Network Short (16-bit)
 *   
 *   Intel x86 (little-endian):  0x1234 stored as [0x34][0x12]
 *   Network (big-endian):       0x1234 stored as [0x12][0x34]
 *   
 *   htons(43594) converts host representation to network representation
 *   
 *   Example on x86:
 *     43594 = 0xAA3A
 *     Host order:    [0x3A][0xAA]
 *     htons result:  [0xAA][0xAA]  (swaps bytes on little-endian)
 * 
 * ERROR HANDLING:
 *   On any failure:
 *     - Cleanup resources (close socket, call WSACleanup)
 *     - Set server_fd = -1
 *     - Return false
 * 
 * COMPLEXITY: O(1) time, O(1) space
 */
bool network_init(NetworkServer* server, u16 port);

/*
 * network_shutdown - Close server socket and cleanup resources
 * 
 * @param server  Pointer to initialized NetworkServer
 * 
 * ALGORITHM:
 *   1. Close server socket (close/closesocket)
 *   2. Cleanup Winsock on Windows (WSACleanup)
 *   3. Set server_fd = -1, running = false
 * 
 * SOCKET CLOSURE:
 *   UNIX:    close(fd)
 *   Windows: closesocket(fd)
 *   
 *   Effect:
 *     - Sends FIN packet to gracefully close connections
 *     - Frees kernel resources (socket buffers)
 *     - Releases port binding
 * 
 * WSACLEANUP (Windows):
 *   - Decrements Winsock reference count
 *   - If count reaches 0, unloads Winsock DLL
 *   - Must match each WSAStartup() with WSACleanup()
 * 
 * CLIENT CONNECTIONS:
 *   This function only closes the LISTENING socket
 *   Client sockets (from accept) must be closed separately
 *   
 *   Proper shutdown sequence:
 *     1. Close all client sockets
 *     2. Call network_shutdown() to close server socket
 * 
 * COMPLEXITY: O(1) time
 */
void network_shutdown(NetworkServer* server);

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
 *   1. Call accept() on server socket (non-blocking)
 *   2. If client socket returned, set it to non-blocking mode
 *   3. Return client socket fd
 * 
 * ACCEPT OPERATION:
 *   int accept(int sockfd, struct sockaddr* addr, socklen_t* addrlen)
 *   
 *   Parameters:
 *     sockfd:  Listening socket (server->server_fd)
 *     addr:    Pointer to sockaddr_in to receive client address
 *     addrlen: Pointer to size of addr structure
 *   
 *   Returns:
 *     >= 0: New socket file descriptor for client communication
 *     -1:   Error (check errno/WSAGetLastError)
 * 
 * CLIENT ADDRESS INFORMATION:
 *   struct sockaddr_in {
 *       sa_family_t    sin_family;  // AF_INET
 *       in_port_t      sin_port;    // Client's source port (network byte order)
 *       struct in_addr sin_addr;    // Client's IP address (network byte order)
 *       char           sin_zero[8]; // Padding (unused)
 *   };
 *   
 *   After accept():
 *     - sin_addr.s_addr contains client IP (e.g., 0xC0A80164 = 192.168.1.100)
 *     - sin_port contains client port (ephemeral, typically 49152-65535)
 * 
 * NON-BLOCKING BEHAVIOR:
 *   If server socket is non-blocking and no connections pending:
 *     - accept() returns -1 immediately
 *     - errno = EAGAIN (POSIX) or EWOULDBLOCK (some systems)
 *     - WSAGetLastError() = WSAEWOULDBLOCK (Windows)
 *   
 *   This allows polling pattern:
 *     while (true) {
 *         i32 client = network_accept_connection(&server);
 *         if (client >= 0) {
 *             printf("New client: %d\n", client);
 *         } else {
 *             // No pending connections, continue main loop
 *             break;
 *         }
 *     }
 * 
 * CLIENT SOCKET NON-BLOCKING MODE:
 *   Newly accepted socket inherits blocking mode from server socket
 *   on some systems, but NOT guaranteed by POSIX
 *   
 *   Safe practice: Explicitly set client socket to non-blocking
 *   
 *   UNIX:    fcntl(client_fd, F_SETFL, O_NONBLOCK)
 *   Windows: ioctlsocket(client_fd, FIONBIO, &mode) where mode=1
 * 
 * SOCKET STATES AFTER ACCEPT:
 * ┌──────────────────────────────────────────────────────────────────┐
 * │ Server Socket (server_fd):                                       │
 * │   State: LISTEN                                                  │
 * │   Purpose: Accept new connections                                │
 * │   Operations: accept() only                                      │
 * │                                                                  │
 * │ Client Socket (return value):                                    │
 * │   State: ESTABLISHED                                             │
 * │   Purpose: Communicate with specific client                      │
 * │   Operations: recv(), send(), close()                            │
 * └──────────────────────────────────────────────────────────────────┘
 * 
 * TCP CONNECTION STATE DIAGRAM:
 * 
 *   Server Socket (listening):
 *   ┌─────────┐
 *   │ CLOSED  │
 *   └────┬────┘
 *        │ socket()
 *   ┌────▼────┐
 *   │  BOUND  │
 *   └────┬────┘
 *        │ listen()
 *   ┌────▼────┐
 *   │ LISTEN  │◄───── stays in LISTEN state
 *   └────┬────┘
 *        │ accept() creates NEW socket:
 *        │
 *   Client Socket (connected):
 *   ┌────▼────────┐
 *   │ ESTABLISHED │ ← Can send/receive data
 *   └─────────────┘
 * 
 * SECURITY CONSIDERATIONS:
 *   - No authentication at TCP level (happens in application layer)
 *   - Accept all connections (application validates client)
 *   - Vulnerable to SYN flood attacks (backlog can fill up)
 *   - Rate limiting should be implemented in application
 * 
 * COMPLEXITY: O(1) time
 */
i32 network_accept_connection(NetworkServer* server);

/*******************************************************************************
 * DATA TRANSFER
 ******************************************************************************/

/*
 * network_receive - Read bytes from client socket (non-blocking)
 * 
 * @param socket_fd  Client socket file descriptor
 * @param buffer     Destination buffer for received data
 * @param length     Maximum bytes to read
 * @return           Bytes received (>0), 0 on disconnect, -1 on error
 * 
 * ALGORITHM:
 *   Call recv(socket_fd, buffer, length, 0) and return result
 * 
 * RECV OPERATION:
 *   ssize_t recv(int sockfd, void* buf, size_t len, int flags)
 *   
 *   Parameters:
 *     sockfd:  Connected socket file descriptor
 *     buf:     Buffer to store received data
 *     len:     Maximum bytes to read
 *     flags:   0 = standard receive, other options:
 *              MSG_PEEK:     Read without removing from queue
 *              MSG_WAITALL:  Wait for full len bytes (blocks)
 *              MSG_DONTWAIT: Non-blocking for this call only
 *   
 *   Returns:
 *     > 0:  Number of bytes received (may be less than len)
 *     0:    Peer performed orderly shutdown (FIN received)
 *     -1:   Error occurred (check errno)
 * 
 * RETURN VALUE INTERPRETATION:
 * 
 * Case 1: bytes > 0 (data received)
 *   buffer[0..bytes-1] contains valid data
 *   
 *   Example:
 *     u8 buf[512];
 *     i32 n = network_receive(fd, buf, 512);
 *     if (n > 0) {
 *         process_packet(buf, n);
 *     }
 * 
 * Case 2: bytes == 0 (graceful disconnect)
 *   Client called close() on their socket
 *   TCP FIN packet received
 *   Connection is half-closed (can't receive more data)
 *   
 *   Action: Close socket and remove client
 *   
 *   Example:
 *     if (n == 0) {
 *         printf("Client %d disconnected\n", fd);
 *         network_close_socket(fd);
 *     }
 * 
 * Case 3: bytes == -1 && errno == EAGAIN (no data available)
 *   Non-blocking socket with no data in receive buffer
 *   NOT an error - normal in non-blocking mode
 *   
 *   Action: Continue to next client
 *   
 *   Example:
 *     if (n < 0) {
 *         if (errno == EAGAIN || errno == EWOULDBLOCK) {
 *             // No data ready, check again later
 *         } else {
 *             // Real error occurred
 *         }
 *     }
 * 
 * Case 4: bytes == -1 && errno != EAGAIN (error)
 *   Connection error occurred:
 *     ECONNRESET:  Connection reset by peer (client crashed)
 *     ETIMEDOUT:   Connection timed out (network issue)
 *     EPIPE:       Broken pipe (writing to closed socket)
 *   
 *   Action: Close socket and remove client
 * 
 * SOCKET RECEIVE BUFFER:
 * ┌──────────────────────────────────────────────────────────────────┐
 * │ KERNEL SPACE (Operating System):                                 │
 * │                                                                  │
 * │  TCP Receive Buffer (typically 64 KB):                           │
 * │  ┌───┬───┬───┬───┬───┬───┬───┬───┬───────────────┐               │
 * │  │ A │ B │ C │ D │ E │ F │ G │ H │     ...       │               │
 * │  └───┴───┴───┴───┴───┴───┴───┴───┴───────────────┘               │
 * │   ↑                           ↑                                  │
 * │   read_ptr               write_ptr (TCP adds data here)          │
 * │                                                                  │
 * │  recv() copies data from read_ptr to user buffer                 │
 * │  Advances read_ptr by bytes copied                               │
 * └──────────────────────────────────────────────────────────────────┘
 * ┌──────────────────────────────────────────────────────────────────┐
 * │ USER SPACE (Application):                                        │
 * │                                                                  │
 * │  u8 buffer[512];                                                 │
 * │  ┌─────────────────────────────────────────┐                     │
 * │  │             (empty)                     │                     │
 * │  └─────────────────────────────────────────┘                     │
 * │                                                                  │
 * │  After recv(fd, buffer, 512):                                    │
 * │  ┌───┬───┬───┬───┬───┬───┬───┬───┬───────┐                       │
 * │  │ A │ B │ C │ D │ E │ F │ G │ H │ ...   │                       │
 * │  └───┴───┴───┴───┴───┴───┴───┴───┴───────┘                      │
 * │                                                                  │
 * └──────────────────────────────────────────────────────────────────┘
 * 
 * PARTIAL READS:
 *   recv() may return fewer bytes than requested
 *   
 *   Example:
 *     recv(fd, buffer, 512) returns 120
 *     Means: 120 bytes copied, possibly more data waiting
 *   
 *   For packet protocols, must handle fragmentation:
 *     1. Read packet header to get total length
 *     2. Loop recv() until full packet received
 *     3. Or buffer partial data until complete
 * 
 * TCP STREAM SEMANTICS:
 *   TCP provides BYTE STREAM, not message boundaries
 *   
 *   Sender sends:    write(10 bytes), write(20 bytes)
 *   Receiver may get: recv(30 bytes) - combined
 *                or: recv(5), recv(15), recv(10) - split
 *   
 *   Application must:
 *     - Define message framing (length headers, delimiters)
 *     - Buffer incomplete messages
 *     - Parse complete messages
 * 
 * BUFFER OVERFLOW PREVENTION:
 *   NEVER recv() more than buffer size:
 *     u8 buf[512];
 *     recv(fd, buf, 512);  ✓ Safe
 *     recv(fd, buf, 1024); ✗ Buffer overflow!
 * 
 * COMPLEXITY: O(1) to O(length) time depending on kernel implementation
 */
i32 network_receive(i32 socket_fd, u8* buffer, u32 length);

/*
 * network_send - Write bytes to client socket (non-blocking)
 * 
 * @param socket_fd  Client socket file descriptor
 * @param buffer     Source buffer containing data to send
 * @param length     Number of bytes to write
 * @return           Bytes sent (>0), 0 if nothing sent, -1 on error
 * 
 * ALGORITHM:
 *   1. Log transmitted bytes (hex dump for debugging)
 *   2. Call send(socket_fd, buffer, length, 0) and return result
 * 
 * SEND OPERATION:
 *   ssize_t send(int sockfd, const void* buf, size_t len, int flags)
 *   
 *   Parameters:
 *     sockfd:  Connected socket file descriptor
 *     buf:     Buffer containing data to send
 *     len:     Number of bytes to write
 *     flags:   0 = standard send, other options:
 *              MSG_NOSIGNAL: Don't raise SIGPIPE on broken connection
 *              MSG_DONTWAIT: Non-blocking for this call only
 *   
 *   Returns:
 *     > 0:  Number of bytes sent (may be less than len)
 *     -1:   Error occurred (check errno)
 * 
 * RETURN VALUE INTERPRETATION:
 * 
 * Case 1: bytes == length (full write)
 *   All data successfully queued in send buffer
 *   
 *   Example:
 *     u8 packet[100];
 *     i32 n = network_send(fd, packet, 100);
 *     if (n == 100) {
 *         // Success
 *     }
 * 
 * Case 2: 0 < bytes < length (partial write)
 *   Send buffer is full, only part of data queued
 *   Common with large writes on non-blocking sockets
 *   
 *   Action: Must retry sending remaining bytes
 *   
 *   Example:
 *     u8 packet[1000];
 *     i32 sent = 0;
 *     while (sent < 1000) {
 *         i32 n = network_send(fd, packet + sent, 1000 - sent);
 *         if (n > 0) {
 *             sent += n;
 *         } else if (n < 0 && errno != EAGAIN) {
 *             break;  // Error
 *         }
 *     }
 * 
 * Case 3: bytes == -1 && errno == EAGAIN (buffer full)
 *   Non-blocking socket with full send buffer
 *   NOT an error - normal in non-blocking mode
 *   
 *   Action: Retry later (or use select/poll for writability)
 * 
 * Case 4: bytes == -1 && errno != EAGAIN (error)
 *   Connection error:
 *     EPIPE:        Connection closed by peer (writes fail)
 *     ECONNRESET:   Connection reset
 *     ENOTCONN:     Socket not connected
 *   
 *   Action: Close socket
 * 
 * SOCKET SEND BUFFER:
 * ┌──────────────────────────────────────────────────────────────────┐
 * │ USER SPACE (Application):                                        │
 * │                                                                  │
 * │  u8 packet[100] = {...};                                         │
 * │  network_send(fd, packet, 100);                                  │
 * │                      ↓                                           │
 * └──────────────────────┼───────────────────────────────────────────┘
 *                        │ copy to kernel
 * ┌──────────────────────▼──────────────────────────────────────────┐
 * │ KERNEL SPACE (Operating System):                                │
 * │                                                                 │
 * │  TCP Send Buffer (typically 16-64 KB):                          │
 * │  ┌───┬───┬───┬───┬───┬───┬───┬───┬───────────────┐              │
 * │  │ . │ . │ . │ . │ . │ . │ . │ . │     ...       │              │
 * │  └───┴───┴───┴───┴───┴───┴───┴───┴───────────────┘              │
 * │   ↑                           ↑                                 │
 * │   sent                     queued (waiting for ACK)             │
 * │                                                                 │
 * │  TCP will:                                                      │
 * │    1. Segment data into packets (MSS typically 1460 bytes)      │
 * │    2. Add TCP headers (source/dest port, seq num, checksum)     │
 * │    3. Wait for ACK from receiver                                │
 * │    4. Retransmit if ACK not received (timeout)                  │
 * │    5. Remove from buffer when ACK received                      │
 * └─────────────────────────────────────────────────────────────────┘
 * 
 * TCP PACKET STRUCTURE:
 * ┌──────────────────────────────────────────────────────────────────┐
 * │ IP Header (20 bytes):                                            │
 * │   Version, Header Length, Total Length, TTL, Protocol, etc.      │
 * ├──────────────────────────────────────────────────────────────────┤
 * │ TCP Header (20-60 bytes):                                        │
 * │   ┌──────────────┬──────────────┬──────────────┬──────────────┐  │
 * │   │ Source Port  │ Dest Port    │ Sequence #   │  Ack #       │  │
 * │   │ (16 bits)    │ (16 bits)    │ (32 bits)    │ (32 bits)    │  │
 * │   ├──────────────┴──────────────┼──────────────┴──────────────┤  │
 * │   │ Flags, Window Size, etc.    │ Checksum, Urgent Pointer    │  │
 * │   └─────────────────────────────┴─────────────────────────────┘  │
 * ├──────────────────────────────────────────────────────────────────┤
 * │ Data (0-65495 bytes):                                            │
 * │   Application data (e.g., RuneScape packet)                      │
 * └──────────────────────────────────────────────────────────────────┘
 * 
 * MAXIMUM SEGMENT SIZE (MSS):
 *   Ethernet MTU: 1500 bytes
 *   - IP header:  20 bytes
 *   - TCP header: 20 bytes
 *   = MSS: 1460 bytes
 *   
 *   If application sends 10000 bytes:
 *     TCP splits into ~7 packets (10000 / 1460 = approximately 7)
 *     Each packet sent separately, ACKed separately
 * 
 * FLOW CONTROL:
 *   Receiver advertises window size (available buffer space)
 *   Sender limits unacknowledged data to window size
 *   Prevents sender from overwhelming receiver
 *   
 *   If receiver buffer full:
 *     - Window size = 0
 *     - Sender stops transmitting
 *     - send() returns EAGAIN (buffer full)
 * 
 * HEX DUMP (for debugging):
 *   This function dumps transmitted bytes in hexadecimal:
 *   
 *   [HEX] TX len=10: 01 02 03 04 05 06 07 08 09 0A
 *   
 *   Useful for:
 *     - Verifying packet structure
 *     - Debugging protocol issues
 *     - Comparing with packet captures (Wireshark)
 * 
 * COMPLEXITY: O(1) to O(length) time
 */
i32 network_send(i32 socket_fd, const u8* buffer, u32 length);

/*******************************************************************************
 * SOCKET CLEANUP
 ******************************************************************************/

/*
 * network_close_socket - Close client socket and free resources
 * 
 * @param socket_fd  Socket file descriptor to close
 * 
 * ALGORITHM:
 *   1. Check if socket_fd is valid (>= 0)
 *   2. Call close() or closesocket() depending on platform
 *   3. OS performs TCP teardown (FIN handshake)
 * 
 * TCP CONNECTION TEARDOWN:
 * 
 *   Application calls close():
 *   ┌────────────────────────────────────────────────────────────┐
 *   │ Active Close Side                 Passive Close Side       │
 *   │     │                                      │               │
 *   │     │──────── FIN (seq=x) ────────────────>│               │
 *   │     │         "I'm done sending"           │               │
 *   │ ┌───▼────┐                              ┌──▼───┐           │
 *   │ │FIN_WAIT│                              │CLOSE │           │
 *   │ │  _1    │                              │_WAIT │           │
 *   │ └───┬────┘                              └──┬───┘           │
 *   │     │                                      │               │
 *   │     │<─────── ACK (ack=x+1) ───────────────│               │
 *   │     │         "I received your FIN"        │               │
 *   │ ┌───▼────┐                                 │               │
 *   │ │FIN_WAIT│                                 │               │
 *   │ │  _2    │                                 │               │
 *   │ └───┬────┘                              ┌──▼───┐           │
 *   │     │                                   │CLOSE │           │
 *   │     │<─────── FIN (seq=y) ──────────────│_WAIT │           │
 *   │     │         "I'm done too"            └──────┘           │
 *   │ ┌───▼────┐                                                 │
 *   │ │TIME    │                                                 │
 *   │ │_WAIT   │                                                 │
 *   │ └───┬────┘                                                 │
 *   │     │──────── ACK (ack=y+1) ───────────────>               │
 *   │     │         "Acknowledged"               │               │
 *   │     │                                   ┌──▼───┐           │
 *   │     │ (wait 2*MSL, typically 60s)       │CLOSED│           │
 *   │ ┌───▼────┐                              └──────┘           │
 *   │ │CLOSED  │                                                 │
 *   │ └────────┘                                                 │
 *   └────────────────────────────────────────────────────────────┘
 * 
 * TIME_WAIT STATE:
 *   After closing, socket enters TIME_WAIT for 2*MSL (Maximum Segment Lifetime)
 *   Typically 60 seconds (2 * 30s)
 *   
 *   Purpose:
 *     - Allows delayed packets to expire (prevents confusion with new connections)
 *     - Ensures final ACK is received
 *   
 *   During TIME_WAIT:
 *     - Port cannot be reused (unless SO_REUSEADDR set)
 *     - Socket still consumes kernel resources
 *   
 *   Problem for servers:
 *     Restart server → bind() fails with "Address in use"
 *   
 *   Solution:
 *     Set SO_REUSEADDR before bind() (see network_init)
 * 
 * RESOURCE CLEANUP:
 *   close() frees:
 *     - File descriptor table entry
 *     - Socket send/receive buffers (64-128 KB typically)
 *     - TCP control block (sequence numbers, window, etc.)
 *     - Routing table entry
 *   
 *   Does NOT free immediately if:
 *     - Data still in send buffer (waits for ACK)
 *     - TIME_WAIT state active
 * 
 * PLATFORM DIFFERENCES:
 *   UNIX:    close(fd)
 *            - Returns 0 on success, -1 on error
 *            - Errors typically ignored (fd already invalid)
 *   
 *   Windows: closesocket(fd)
 *            - Returns 0 on success, SOCKET_ERROR (-1) on error
 *            - Must use instead of CloseHandle()
 * 
 * ABORTIVE CLOSE:
 *   This function performs graceful close (FIN handshake)
 *   
 *   For abortive close (RST packet, immediate termination):
 *     struct linger opt = {1, 0};  // l_onoff=1, l_linger=0
 *     setsockopt(fd, SOL_SOCKET, SO_LINGER, &opt, sizeof(opt));
 *     close(fd);  // Sends RST instead of FIN
 *   
 *   Use abortive close when:
 *     - Client misbehaving (flood attack)
 *     - Application error (invalid state)
 *     - Immediate shutdown required
 * 
 * SHUTDOWN VS CLOSE:
 *   shutdown(fd, SHUT_WR):  Stop sending, but can still receive
 *   shutdown(fd, SHUT_RD):  Stop receiving, but can still send
 *   shutdown(fd, SHUT_RDWR): Stop both, but socket still open
 *   close(fd):              Shutdown + free resources
 *   
 *   Typical pattern for graceful close:
 *     shutdown(fd, SHUT_WR);  // Send FIN
 *     while (recv(fd, buf, sizeof(buf)) > 0);  // Drain receive buffer
 *     close(fd);  // Free resources
 * 
 * COMPLEXITY: O(1) time
 */
void network_close_socket(i32 socket_fd);

#endif /* NETWORK_H */
