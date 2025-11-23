/*
 * ==============================================================================
 * login.c - RuneScape Login Protocol Implementation
 * ==============================================================================
 *
 * PURPOSE
 * -------
 * Implements the three-stage login handshake for RuneScape protocol revision
 * 225. Handles ISAAC cipher initialization, credential validation, and player
 * authentication. This file contains the core server-side login logic.
 *
 * ARCHITECTURE OVERVIEW
 * ---------------------
 * The login system is designed as a state machine with three distinct stages:
 *
 *   1. CONNECTION : Generate and send server seeds for ISAAC
 *   2. HEADER     : Receive and validate client credentials
 *   3. PAYLOAD    : Process additional data (currently unused)
 *
 * Each stage has strict requirements and validation rules to ensure protocol
 * compliance and security.
 *
 * KEY ALGORITHMS
 * --------------
 *
 * 1. ISAAC Cipher Initialization
 *    ---------------------------
 *    The ISAAC (Indirection, Shift, Accumulate, Add, and Count) cipher is a
 *    cryptographically secure PRNG used to obfuscate packet opcodes.
 *
 *    Seed Derivation:
 *      Client generates 4x 32-bit seeds: [S0, S1, S2, S3]
 *      
 *      Server creates two cipher instances:
 *        in_cipher  : Decodes client opcodes with seeds [S0, S1, S2, S3]
 *        out_cipher : Encodes server opcodes with seeds [S0+50, S1+50, S2+50, S3+50]
 *
 *    The +50 offset ensures different keystreams for bidirectional communication,
 *    preventing potential weaknesses from using identical ciphers.
 *
 *    Usage in Packet Protocol:
 *      When client sends opcode:
 *        encrypted_opcode = original_opcode XOR isaac_next(&client_out_cipher)
 *        
 *      When server receives opcode:
 *        original_opcode = encrypted_opcode XOR isaac_next(&server_in_cipher)
 *
 *    Security Properties:
 *      - Period: 2^8295 (effectively never repeats)
 *      - Speed: Very fast (no multiplication, uses only XOR/ADD/SHIFT)
 *      - Passes DIEHARD randomness tests
 *
 * 2. Protocol Validation
 *    -------------------
 *    The server performs multiple validation checks:
 *
 *      a) Login Type Validation:
 *         - Must be 16 (normal login) or 18 (reconnect)
 *         - Reject: Any other value
 *
 *      b) Version Check:
 *         - Client version must be exactly 225
 *         - Reject: Prevents outdated or modified clients
 *
 *      c) Block Length Verification:
 *         - Ensures complete packet received
 *         - Prevents partial read errors
 *
 *      d) Credential Extraction:
 *         - Username: Read until byte 10 (newline)
 *         - Password: Read until byte 10 (newline)
 *         - Bounded by MAX_USERNAME_LENGTH (20) and 63
 *
 * 3. Random Seed Generation
 *    -----------------------
 *    Server generates two 32-bit seeds using rand() and sends to client.
 *    
 *    SECURITY WARNING:
 *      The C standard library rand() is NOT cryptographically secure.
 *      It uses a Linear Congruential Generator (LCG) with predictable
 *      output. An attacker who observes multiple seeds could predict
 *      future values.
 *
 *    Recommended Alternatives:
 *      - /dev/urandom (Unix/Linux)
 *      - CryptGenRandom (Windows)
 *      - OpenSSL RAND_bytes()
 *      - libsodium randombytes_buf()
 *
 * PROTOCOL SPECIFICATION: LOGIN HEADER
 * ------------------------------------
 * The login header is sent from client to server after receiving server seeds.
 * All multi-byte values are big-endian.
 *
 *   Offset | Length | Type   | Description
 *   -------+--------+--------+----------------------------------------------
 *   0      | 1      | u8     | Login type (16 or 18)
 *   1      | 1      | u8     | Block length (N)
 *   2      | 1      | u8     | Client version (must be 225)
 *   3      | 1      | u8     | Memory flag (0=low, 1=high)
 *   4      | 36     | u32[9] | CRC32 checksums for cache validation
 *   40     | 1      | u8     | RSA block length
 *   41     | 1      | u8     | RSA opcode (10)
 *   42     | 16     | u32[4] | Client ISAAC seeds
 *   58     | 4      | u32    | Unique identifier (UID)
 *   62     | Var    | str    | Username (newline-terminated)
 *   62+U   | Var    | str    | Password (newline-terminated)
 *
 * ASCII Diagram of Login Header:
 *
 *   0       1       2       3       4       5       6       7       8
 *   +-------+-------+-------+-------+-------+-------+-------+-------+
 *   | Type  | Len   | Ver   | Mem   |        CRC32 Checksum 0       |
 *   +-------+-------+-------+-------+-------+-------+-------+-------+
 *   |           CRC32 Checksum 1            | CRC32 Checksum 2  ... |
 *   +-------+-------+-------+-------+-------+-------+-------+-------+
 *   |   ... (7 more CRC32 checksums - total 9)                      |
 *   +-------+-------+-------+-------+-------+-------+-------+-------+
 *   | RSA L | RSA Op|         Client ISAAC Seed 0 (32-bit)          |
 *   +-------+-------+-------+-------+-------+-------+-------+-------+
 *   |  Seed 1 (32)  |  Seed 2 (32)  |  Seed 3 (32)  |  UID (32-bit) |
 *   +-------+-------+-------+-------+-------+-------+-------+-------+
 *   | Username (variable, terminated by 0x0A)                       |
 *   +-------+-------+-------+-------+-------+-------+-------+-------+
 *   | Password (variable, terminated by 0x0A)                       |
 *   +-------+-------+-------+-------+-------+-------+-------+-------+
 *
 * SECURITY ANALYSIS
 * -----------------
 *
 * 1. Password Transmission:
 *    - Passwords sent in plaintext within RSA block
 *    - Current implementation does NOT decrypt RSA (reads plaintext)
 *    - VULNERABILITY: Network sniffing reveals passwords
 *    - Mitigation: Implement TLS/SSL or proper RSA decryption
 *
 * 2. Password Storage:
 *    - Passwords stored in Player struct as plaintext
 *    - VULNERABILITY: Memory dumps reveal passwords
 *    - Mitigation: Hash with bcrypt/argon2, clear from memory after validation
 *
 * 3. Timing Attacks:
 *    - strcmp() used for password comparison leaks length
 *    - VULNERABILITY: Attacker can measure timing to guess password
 *    - Mitigation: Use constant-time comparison (e.g., memcmp with fixed length)
 *
 * 4. Seed Predictability:
 *    - rand() seeded with time(NULL) is predictable
 *    - VULNERABILITY: If attacker knows approximate time, can predict seeds
 *    - Mitigation: Use cryptographically secure RNG
 *
 * 5. No Rate Limiting:
 *    - No delay between failed login attempts
 *    - VULNERABILITY: Brute-force attacks possible
 *    - Mitigation: Add exponential backoff or temporary IP ban
 *
 * COMPLEXITY ANALYSIS
 * -------------------
 *
 * login_process_connection():
 *   Time  : O(1) - Fixed operations (rand, buffer writes, send)
 *   Space : O(1) - Fixed 8-byte buffer
 *
 * login_process_header():
 *   Time  : O(U + P) where U=username length, P=password length
 *           - Buffer reads: O(1) per byte
 *           - ISAAC init: O(1) (fixed 256 rounds internally)
 *           - Total: O(U + P)
 *   Space : O(1) - Fixed-size arrays and buffers
 *
 * login_send_initial_packets():
 *   Time  : O(1) - Fixed flag assignments
 *   Space : O(1) - No allocations
 *
 * MEMORY LAYOUT
 * -------------
 * Player Structure (relevant fields):
 *
 *   +-------------------+
 *   | socket_fd (int)   |  File descriptor for client socket
 *   +-------------------+
 *   | username[21]      |  Null-terminated username (max 20 chars)
 *   +-------------------+
 *   | password[64]      |  Null-terminated password (max 63 chars)
 *   +-------------------+
 *   | in_cipher (ISAAC) |  Decrypts incoming packet opcodes
 *   +-------------------+
 *   | out_cipher(ISAAC) |  Encrypts outgoing packet opcodes
 *   +-------------------+
 *   | state (enum)      |  CONNECTED, LOGGING_IN, LOGGED_IN, etc.
 *   +-------------------+
 *   | update_flags      |  Bitfield for appearance, movement, etc.
 *   +-------------------+
 *
 * CROSS-REFERENCES
 * ----------------
 * - buffer.h/buffer.c : Stream buffer I/O (buffer_read_*, buffer_write_*)
 * - network.h/network.c : Socket operations (network_send)
 * - isaac.h/isaac.c : ISAAC cipher (isaac_init, isaac_next)
 * - player.h/player.c : Player state (Player struct, PLAYER_STATE_*)
 * - world.h/world.c : World management (world_register_player)
 *
 * EXAMPLE USAGE
 * -------------
 *   // Server main loop
 *   int client_socket = accept(server_socket, NULL, NULL);
 *   Player* player = player_create(client_socket, next_index++);
 *   
 *   // Stage 1: Send seeds
 *   if (!login_process_connection(player)) {
 *       fprintf(stderr, "Failed to send seeds\n");
 *       player_destroy(player);
 *       close(client_socket);
 *       return;
 *   }
 *   
 *   // Stage 2: Wait for login header (in select/epoll loop)
 *   StreamBuffer* in = buffer_create(512);
 *   ssize_t received = network_receive(client_socket, in->data, 512);
 *   in->position = received;
 *   in->bit_position = 0;
 *   
 *   if (!login_process_header(player, in)) {
 *       fprintf(stderr, "Login failed for %s\n", player->username);
 *       buffer_destroy(in);
 *       player_destroy(player);
 *       close(client_socket);
 *       return;
 *   }
 *   buffer_destroy(in);
 *   
 *   // Stage 3: Finalize login
 *   login_send_initial_packets(player);
 *   
 *   // Player now in game, process normal packets
 *   printf("Player %s ready for game\n", player->username);
 *
 * ==============================================================================
 */

#include "login.h"
#include "network.h"
#include "world.h"
#include "player_save.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/*
 * ==============================================================================
 * login_process_connection - Send ISAAC Seeds to Client (Stage 1)
 * ==============================================================================
 *
 * ALGORITHM
 * ---------
 * This function implements the first stage of the login handshake by generating
 * two random 32-bit seeds and transmitting them to the client. These seeds are
 * used by the client to initialize its ISAAC ciphers, which will be used for
 * all subsequent packet opcode encryption.
 *
 * Step-by-Step Process:
 *
 *   1. Allocate 8-byte output buffer
 *      - 4 bytes for seed1 (u32)
 *      - 4 bytes for seed2 (u32)
 *
 *   2. Seed random number generator with current time
 *      - srand(time(NULL))
 *      - WARNING: time(NULL) provides only second-level granularity
 *      - Multiple connections in same second get same seed!
 *
 *   3. Generate two random 32-bit values
 *      - seed1 = rand()
 *      - seed2 = rand()
 *      - WARNING: rand() is NOT cryptographically secure
 *
 *   4. Write seeds to buffer in big-endian byte order
 *      - buffer_write_int(out, seed1, BYTE_ORDER_BIG)
 *      - buffer_write_int(out, seed2, BYTE_ORDER_BIG)
 *      - Big-endian ensures cross-platform compatibility
 *
 *   5. Transmit buffer to client socket
 *      - network_send(socket, data, length)
 *      - Blocking send (waits until all 8 bytes sent or error)
 *
 *   6. Cleanup and return status
 *      - buffer_destroy(out) frees memory
 *      - Return true if sent > 0, false otherwise
 *
 * PACKET STRUCTURE
 * ----------------
 *   Server Seed Packet (8 bytes, big-endian):
 *
 *     Byte Offset: 0       1       2       3       4       5       6       7
 *                  +-------+-------+-------+-------+-------+-------+-------+-------+
 *                  |           Seed 1 (u32)        |           Seed 2 (u32)        |
 *                  +-------+-------+-------+-------+-------+-------+-------+-------+
 *
 *   Example (hexadecimal):
 *     Seed1 = 0x12345678, Seed2 = 0xABCDEF01
 *     Wire format: 12 34 56 78 AB CD EF 01
 *
 * SECURITY CONCERNS
 * -----------------
 *   1. Predictable Seeds:
 *      - rand() is a Linear Congruential Generator (LCG)
 *      - Formula: next = (a * prev + c) mod m
 *      - Attacker can predict future seeds if they observe enough values
 *
 *   2. Low Entropy:
 *      - srand(time(NULL)) has at most 32 bits of entropy
 *      - Multiple connections in same second share seed
 *      - Attacker can guess seed by knowing connection time
 *
 *   3. Recommended Fix:
 *      - Use /dev/urandom on Unix:
 *          int fd = open("/dev/urandom", O_RDONLY);
 *          read(fd, &seed1, sizeof(seed1));
 *          read(fd, &seed2, sizeof(seed2));
 *          close(fd);
 *      - Or use a crypto library (OpenSSL, libsodium)
 *
 * PARAMETERS
 * ----------
 *   player : Pointer to Player structure
 *            - Must have valid socket_fd
 *            - Must be in PLAYER_STATE_CONNECTED (not enforced)
 *
 * RETURN VALUE
 * ------------
 *   true  : Seeds successfully sent (sent > 0)
 *   false : Network error (send failed or returned 0)
 *
 * SIDE EFFECTS
 * ------------
 *   - Seeds random number generator with current time
 *   - Sends 8 bytes to client socket
 *   - Prints debug message to stdout
 *   - Does NOT store seeds in player structure
 *
 * ERROR HANDLING
 * --------------
 *   - network_send returns -1 on error (errno set)
 *   - network_send returns 0 if connection closed by peer
 *   - Buffer allocation failure would abort (buffer_create asserts)
 *
 * EXAMPLE
 * -------
 *   Player* player = player_create(client_socket, 1);
 *   if (!login_process_connection(player)) {
 *       fprintf(stderr, "Failed to send seeds, closing connection\n");
 *       player_destroy(player);
 *       return;
 *   }
 *   printf("Seeds sent, awaiting login header\n");
 *
 * TIME COMPLEXITY
 * ---------------
 *   O(1) - Constant time operations:
 *     - rand(): O(1)
 *     - buffer writes: O(1)
 *     - network send: O(1) for fixed 8 bytes
 *
 * SPACE COMPLEXITY
 * ----------------
 *   O(1) - Fixed 8-byte buffer allocation
 */
bool login_process_connection(Player* player) {
    /* Allocate output buffer for 8 bytes (2x u32) */
    StreamBuffer* out = buffer_create(8);
    
    /* 
     * Seed the random number generator with current Unix timestamp.
     * WARNING: This is called for EVERY connection, reseeding with same
     * value if multiple connections occur in the same second. This reduces
     * randomness and makes seeds predictable.
     * 
     * BETTER APPROACH: Seed once at server startup, or use better RNG.
     */
    srand(time(NULL));
    
    /* Generate two pseudo-random 32-bit seeds */
    u32 seed1 = rand();
    u32 seed2 = rand();
    
    /* 
     * Write seeds to buffer in big-endian byte order.
     * Big-endian ensures compatibility with C client (Client3-main uses big-endian).
     * 
     * Example:
     *   seed1 = 0x12345678
     *   Buffer after write: [12, 34, 56, 78]
     */
    buffer_write_int(out, seed1, BYTE_ORDER_BIG);
    buffer_write_int(out, seed2, BYTE_ORDER_BIG);
    
    /* 
     * Send 8-byte seed packet to client.
     * network_send() returns number of bytes sent, or -1/0 on error.
     */
    i32 sent = network_send(player->socket_fd, out->data, out->position);
    
    /* Free buffer memory (no longer needed) */
    buffer_destroy(out);
    
    /* Log success for debugging */
    if (sent > 0) {
        printf("Sent server seed to player %u\n", player->index);
        return true;
    }
    
    /* Network error or connection closed */
    return false;
}

/*
 * ==============================================================================
 * login_process_header - Validate Credentials and Initialize Ciphers (Stage 2)
 * ==============================================================================
 *
 * ALGORITHM
 * ---------
 * This is the most complex stage of the login process. It parses the login
 * header packet, validates protocol compliance, extracts credentials, and
 * initializes ISAAC ciphers for bidirectional communication.
 *
 * Step-by-Step Process:
 *
 *   1. Validate minimum packet length (2 bytes)
 *      - Need login_type + block_length before proceeding
 *      - Return false if incomplete (wait for more data)
 *
 *   2. Read and validate login type
 *      - Expected: 16 (normal login) or 18 (reconnect)
 *      - Reject: Any other value (unknown login type)
 *
 *   3. Read block length and validate buffer has enough data
 *      - block_length indicates remaining bytes in packet
 *      - Return false if incomplete (wait for more data)
 *
 *   4. Read and validate client version
 *      - Expected: 225 (RuneScape 2, circa 2004)
 *      - Reject: Prevents version mismatches and modded clients
 *
 *   5. Skip memory flag (1 byte)
 *      - Indicates client memory mode (low/high detail)
 *      - Server doesn't need this information
 *
 *   6. Read and skip CRC32 checksums (9x u32 = 36 bytes)
 *      - Used for cache file validation
 *      - Current implementation doesn't validate (trusts client)
 *      - Production should verify to detect modified caches
 *
 *   7. Skip RSA block header (2 bytes)
 *      - rsa_length: Length of encrypted block
 *      - rsa_opcode: Always 10 for login
 *      - Current implementation doesn't decrypt (assumes plaintext)
 *
 *   8. Read client ISAAC seeds (4x u32 = 16 bytes)
 *      - Client generates random seeds
 *      - Server uses these to initialize both ciphers
 *      - Logged for debugging
 *
 *   9. Skip UID (1x u32 = 4 bytes)
 *      - Unique identifier for client machine
 *      - Could be used for ban enforcement (not implemented)
 *
 *  10. Extract username (variable length, newline-terminated)
 *      - Read characters until byte 10 (newline) or max length
 *      - Null-terminate string
 *      - Max length: MAX_USERNAME_LENGTH (20 characters)
 *
 *  11. Extract password (variable length, newline-terminated)
 *      - Read characters until byte 10 (newline) or max length
 *      - Null-terminate string
 *      - Max length: 63 characters
 *
 *  12. Initialize ISAAC ciphers
 *      - in_cipher: Uses client seeds [S0, S1, S2, S3]
 *      - out_cipher: Uses seeds [S0+50, S1+50, S2+50, S3+50]
 *      - Different seeds prevent keystream collision
 *
 *  13. Send login response code
 *      - LOGIN_RESPONSE_OK (2) for success
 *      - Other codes for errors (not implemented)
 *
 *  14. Update player state
 *      - Set state to PLAYER_STATE_LOGGED_IN
 *      - Player now ready for game protocol
 *
 * LOGIN TYPE CODES
 * ----------------
 *   Code | Meaning
 *   -----+---------------------------------------------------------
 *    16  | Normal login (new session)
 *    18  | Reconnect login (resume existing session)
 *
 *   The server currently treats both types identically. A full implementation
 *   would attempt to restore session state for reconnect requests.
 *
 * CRC32 CHECKSUMS
 * ---------------
 *   The client sends 9 CRC32 checksums to verify cache file integrity:
 *
 *     Index | Cache File
 *     ------+--------------------
 *       0   | title.jag
 *       1   | config.jag
 *       2   | interface.jag
 *       3   | media.jag
 *       4   | versionlist.jag
 *       5   | textures.jag
 *       6   | wordenc.jag
 *       7   | sounds.jag
 *       8   | (unused)
 *
 *   The server should verify these against known good values to detect:
 *     - Modified clients (cheating)
 *     - Corrupted cache files (version mismatch)
 *     - Outdated clients (auto-update needed)
 *
 *   Current implementation IGNORES these values (security risk).
 *
 * ISAAC CIPHER SEED DERIVATION
 * -----------------------------
 *   Client generates 4 random 32-bit seeds and sends to server.
 *   Server creates two ISAAC instances with different seeds:
 *
 *     in_cipher (decode incoming opcodes):
 *       Seeds: [S0, S1, S2, S3]
 *
 *     out_cipher (encode outgoing opcodes):
 *       Seeds: [S0+50, S1+50, S2+50, S3+50]
 *
 *   Why +50 offset?
 *     - Ensures different keystreams for client->server and server->client
 *     - Prevents potential cryptographic weaknesses from identical streams
 *     - Value 50 is arbitrary (any non-zero offset works)
 *
 *   Visualization:
 *     Client sends: [0xAAAAAAAA, 0xBBBBBBBB, 0xCCCCCCCC, 0xDDDDDDDD]
 *
 *     Server in_cipher:  [0xAAAAAAAA, 0xBBBBBBBB, 0xCCCCCCCC, 0xDDDDDDDD]
 *     Server out_cipher: [0xAAAAAAAE, 0xBBBBBBEF, 0xCCCCCCFE, 0xDDDDDE07]
 *                         (each seed + 50 = 0x32)
 *
 * USERNAME/PASSWORD EXTRACTION
 * ----------------------------
 *   Both username and password are stored as ASCII strings terminated by
 *   byte 10 (newline character '\n').
 *
 *   Extraction Algorithm:
 *     1. Initialize length counter to 0
 *     2. Read next byte from buffer
 *     3. If byte == 10 or length >= max, stop
 *     4. Otherwise, store byte in string and increment length
 *     5. Null-terminate string
 *
 *   Example:
 *     Buffer: ['z', 'e', 'z', 'i', 'm', 'a', 10, 'p', 'a', 's', 's', 10]
 *     Username: "zezima"
 *     Password: "pass"
 *
 *   Edge Cases:
 *     - Username with no newline: Reads until MAX_USERNAME_LENGTH (20)
 *     - Password with no newline: Reads until 63 characters
 *     - Empty username: length = 0, null-terminated empty string
 *
 * SECURITY VULNERABILITIES
 * ------------------------
 *   1. No RSA Decryption:
 *      - Client expects sensitive data to be encrypted
 *      - Server reads plaintext (development simplification)
 *      - RISK: Network sniffing reveals passwords
 *
 *   2. Plaintext Password Storage:
 *      - Password copied to player->password
 *      - RISK: Memory dumps reveal passwords
 *      - FIX: Hash immediately, clear plaintext
 *
 *   3. No Credential Validation:
 *      - Always sends LOGIN_RESPONSE_OK
 *      - RISK: Any username/password accepted
 *      - FIX: Query database, verify hash
 *
 *   4. No Rate Limiting:
 *      - No delay between attempts
 *      - RISK: Brute-force attacks
 *      - FIX: Exponential backoff, IP bans
 *
 *   5. Timing Attacks:
 *      - strcmp() leaks password length
 *      - RISK: Side-channel information leak
 *      - FIX: Constant-time comparison
 *
 * PARAMETERS
 * ----------
 *   player : Pointer to Player structure (modified)
 *            - Must have valid socket_fd
 *            - username and password fields will be populated
 *            - in_cipher and out_cipher will be initialized
 *            - state will be set to PLAYER_STATE_LOGGED_IN
 *
 *   in     : Input StreamBuffer containing login header
 *            - Must have at least 2 bytes initially
 *            - Will be fully consumed (position advances)
 *
 * RETURN VALUE
 * ------------
 *   true  : Login successful, player authenticated
 *   false : Login failed (validation error or network error)
 *
 * SIDE EFFECTS
 * ------------
 *   - Reads multiple bytes from input buffer (position advances)
 *   - Writes username to player->username
 *   - Writes password to player->password (PLAINTEXT - security risk)
 *   - Initializes player->in_cipher with client seeds
 *   - Initializes player->out_cipher with client seeds + 50
 *   - Sets player->state to PLAYER_STATE_LOGGED_IN
 *   - Sends 1 byte (LOGIN_RESPONSE_OK) to client socket
 *   - Prints multiple debug messages (seeds, username, success)
 *
 * ERROR HANDLING
 * --------------
 *   Returns false if:
 *     - Buffer has insufficient data (incomplete packet)
 *     - Login type not 16 or 18 (invalid protocol)
 *     - Client version not 225 (version mismatch)
 *     - network_send fails (connection error)
 *
 * EXAMPLE
 * -------
 *   StreamBuffer* in = buffer_create(512);
 *   ssize_t received = recv(player->socket_fd, in->data, 512, 0);
 *   in->limit = received;
 *   
 *   if (login_process_header(player, in)) {
 *       printf("Login successful: %s\n", player->username);
 *       login_send_initial_packets(player);
 *   } else {
 *       printf("Login failed\n");
 *       close(player->socket_fd);
 *   }
 *   buffer_destroy(in);
 *
 * TIME COMPLEXITY
 * ---------------
 *   O(U + P) where U = username length, P = password length
 *     - Fixed reads: O(1)
 *     - Username extraction: O(U)
 *     - Password extraction: O(P)
 *     - ISAAC init: O(1) (fixed internal operations)
 *
 * SPACE COMPLEXITY
 * ----------------
 *   O(1) - All data stored in player struct (fixed size)
 */
bool login_process_header(Player* player, StreamBuffer* in) {
    /* Validate minimum packet size (login_type + block_length) */
    if (buffer_get_remaining(in) < 2) {
        return false;  /* Incomplete packet, wait for more data */
    }
    
    /* 
     * Read login type (1 byte).
     * Expected values:
     *   16 - Normal login (new session)
     *   18 - Reconnect login (resume session)
     */
    u8 login_type = buffer_read_byte(in, false);
    if (login_type != 16 && login_type != 18) {
        printf("Invalid login type: %u\n", login_type);
        return false;  /* Unknown login type, reject */
    }
    
    /* 
     * Read block length (1 byte).
     * Indicates number of bytes remaining in login packet.
     * Used to validate we have complete packet before parsing.
     */
    u8 block_length = buffer_read_byte(in, false);
    if (buffer_get_remaining(in) < block_length) {
        return false;  /* Incomplete packet, wait for more data */
    }
    
    /* 
     * Read client version (1 byte).
     * Must be exactly 225 for this server implementation.
     * Rejects outdated or modified clients.
     */
    u8 client_version = buffer_read_byte(in, false);
    if (client_version != 225) {
        printf("Invalid client version: %u (expected 225)\n", client_version);
        return false;  /* Version mismatch, reject */
    }
    
    /* 
     * Read and discard memory flag (1 byte).
     * Indicates client graphics mode (0=low memory, 1=high memory).
     * Server doesn't need this information.
     */
    buffer_read_byte(in, false);
    
    /* 
     * Read and discard 9 CRC32 checksums (36 bytes total).
     * These validate cache file integrity:
     *   - Index 0-8: Various .jag archive files
     * 
     * SECURITY NOTE: Current implementation doesn't validate these.
     * Production server should verify against known good values to
     * detect modified clients (anti-cheat).
     */
    for (int i = 0; i < 9; i++) {
        buffer_read_int(in, BYTE_ORDER_BIG);
    }
    
    /* 
     * Read RSA block header (2 bytes).
     * In production, this block would be RSA-encrypted with server's
     * public key. Current implementation assumes plaintext for simplicity.
     */
    u8 rsa_length = buffer_read_byte(in, false);  /* Length of encrypted block */
    u8 rsa_opcode = buffer_read_byte(in, false);  /* Always 10 for login */
    
    /* 
     * Read 4 client ISAAC seeds (16 bytes total).
     * Client generates these random values.
     * Server uses them to initialize ISAAC ciphers for opcode encryption.
     * 
     * These seeds establish the shared secret for the session.
     */
    u32 client_seeds[4];
    for (int i = 0; i < 4; i++) {
        client_seeds[i] = buffer_read_int(in, BYTE_ORDER_BIG);
    }
    
    /* Log seeds for debugging (useful for protocol analysis) */
    printf("DEBUG: Client ISAAC seeds: [0x%08X, 0x%08X, 0x%08X, 0x%08X]\n", 
           client_seeds[0], client_seeds[1], client_seeds[2], client_seeds[3]);
    
    /* 
     * Read and discard UID (4 bytes).
     * Unique identifier for client machine.
     * Could be used for:
     *   - Device fingerprinting
     *   - Multi-account detection
     *   - Ban enforcement
     * Current implementation ignores it.
     */
    buffer_read_int(in, BYTE_ORDER_BIG);
    
    /* 
     * Extract username (variable length, newline-terminated).
     * 
     * Format: ASCII characters followed by byte 10 (newline)
     * Max length: MAX_USERNAME_LENGTH (20 characters)
     * 
     * Example: "zezima" encoded as [122, 101, 122, 105, 109, 97, 10]
     */
    char ch;
    u32 username_len = 0;
    while ((ch = buffer_read_byte(in, false)) != 10 && username_len < MAX_USERNAME_LENGTH) {
        player->username[username_len++] = ch;
    }
    player->username[username_len] = '\0';  /* Null-terminate string */
    
    /* 
     * Extract password (variable length, newline-terminated).
     * 
     * Format: ASCII characters followed by byte 10 (newline)
     * Max length: 63 characters
     * 
     * SECURITY WARNING: Stored in plaintext in player structure.
     * Production code should:
     *   1. Hash password immediately (bcrypt, argon2)
     *   2. Compare hash with database
     *   3. Clear plaintext from memory
     */
    u32 password_len = 0;
    while ((ch = buffer_read_byte(in, false)) != 10 && password_len < 63) {
        player->password[password_len++] = ch;
    }
    player->password[password_len] = '\0';  /* Null-terminate string */
    
    /* Log username for debugging (password not logged for security) */
    printf("Login: username='%s'\n", player->username);
    
    /* 
     * Initialize ISAAC ciphers for bidirectional communication.
     * 
     * Two separate ciphers prevent keystream collision:
     *   - in_cipher  : Decodes client opcodes (seeds unmodified)
     *   - out_cipher : Encodes server opcodes (seeds + 50)
     * 
     * The +50 offset ensures different keystreams for each direction.
     */
    u32 in_seed[4];
    u32 out_seed[4];
    for (int i = 0; i < 4; i++) {
        in_seed[i] = client_seeds[i];
        out_seed[i] = client_seeds[i] + 50;  /* Offset for different keystream */
    }
    
    /* Initialize ciphers with derived seeds */
    isaac_init(&player->in_cipher, in_seed, 4);
    isaac_init(&player->out_cipher, out_seed, 4);
    
    /* Log cipher initialization status */
    printf("DEBUG: ISAAC initialized - in_cipher.initialized=%u, out_cipher.initialized=%u\n",
           player->in_cipher.initialized, player->out_cipher.initialized);
    
    /* 
     * Send login response code to client.
     * 
     * Response Code: LOGIN_RESPONSE_OK (2)
     * 
     * Other possible codes:
     *   3  - Invalid credentials
     *   5  - Account already online
     *   15 - Reconnect acknowledged
     *   18 - Staff member login
     * 
     * Current implementation always sends OK (no validation).
     */
    StreamBuffer* out = buffer_create(16);
    buffer_write_byte(out, LOGIN_RESPONSE_OK);
    
    /* Send response to client socket */
    i32 sent = network_send(player->socket_fd, out->data, out->position);
    buffer_destroy(out);
    
    if (sent > 0) {
        /* Load player data from disk (or initialize new player) */
        bool existing_player = player_load(player);
        
        /* Update player state to logged in */
        player->state = PLAYER_STATE_LOGGED_IN;
        
        /* Set login timestamp */
        player->last_login = (u64)time(NULL) * 1000;  /* Convert to milliseconds */
        
        if (existing_player) {
            printf("Player '%s' logged in successfully (existing player loaded)\n", player->username);
        } else {
            printf("Player '%s' logged in successfully (new player created)\n", player->username);
        }
        
        return true;
    }
    
    /* Network error (connection closed or send failed) */
    return false;
}

/*
 * ==============================================================================
 * login_process_payload - Process Additional Login Data (Stage 3)
 * ==============================================================================
 *
 * ALGORITHM
 * ---------
 * This function is reserved for processing additional payload data after the
 * login header has been validated. In protocol revision 225, no payload is
 * sent, so this function immediately returns success.
 *
 * FUTURE USE CASES
 * ----------------
 * This function could be extended to handle:
 *
 *   1. Character Selection:
 *      - For multi-character accounts
 *      - Client sends selected character index
 *      - Server loads character save data
 *
 *   2. Client Settings:
 *      - Graphics preferences (brightness, roofs, etc.)
 *      - Audio settings (music volume, sound effects)
 *      - Control mappings (keybinds)
 *
 *   3. Anti-Cheat Data:
 *      - System information (OS, CPU, RAM)
 *      - Process list (detect bot clients)
 *      - File integrity checksums
 *
 *   4. Session Recovery:
 *      - For reconnect login type (18)
 *      - Client sends previous session token
 *      - Server restores player state
 *
 * PROTOCOL EXTENSION EXAMPLE
 * --------------------------
 * If character selection were implemented:
 *
 *   Payload Structure:
 *     Offset | Length | Type | Description
 *     -------+--------+------+--------------------------------
 *     0      | 1      | u8   | Character count (N)
 *     1      | 1      | u8   | Selected character index
 *     2      | N*22   | str  | Character names (null-padded)
 *
 *   Code:
 *     u8 char_count = buffer_read_byte(in, false);
 *     u8 selected = buffer_read_byte(in, false);
 *     
 *     if (selected >= char_count) {
 *         return false;  // Invalid selection
 *     }
 *     
 *     // Load character save data
 *     if (!player_load_character(player, selected)) {
 *         return false;  // Database error
 *     }
 *
 * PARAMETERS
 * ----------
 *   player : Pointer to Player structure (unused currently)
 *   in     : Input StreamBuffer containing payload data (unused currently)
 *
 * RETURN VALUE
 * ------------
 *   true : Always (no payload processing implemented)
 *
 * SIDE EFFECTS
 * ------------
 *   None currently.
 *
 * TIME COMPLEXITY
 * ---------------
 *   O(1) - Immediate return
 *
 * SPACE COMPLEXITY
 * ----------------
 *   O(1) - No allocations
 */
bool login_process_payload(Player* player, StreamBuffer* in) {
    /* No payload processing for protocol revision 225 */
    return true;
}

/*
 * ==============================================================================
 * login_send_initial_packets - Finalize Login and Prepare Player for Game
 * ==============================================================================
 *
 * ALGORITHM
 * ---------
 * This function performs post-authentication initialization to prepare the
 * player for entering the game world. It sets various flags that trigger
 * initial updates in the game loop.
 *
 * Step-by-Step Process:
 *
 *   1. Set needs_placement flag
 *      - Triggers region loading in next game tick
 *      - Server sends local player position
 *
 *   2. Set region_changed flag
 *      - Triggers map chunk transmission
 *      - Client receives terrain, objects, NPCs for current region
 *
 *   3. Set update_flags to UPDATE_APPEARANCE (0x1)
 *      - Triggers appearance update in next player sync
 *      - Other players receive this player's look/equipment
 *
 *   4. Register player with world
 *      - Adds player to global player list (g_world)
 *      - Indexes by username for lookup (friends, PMs, trading)
 *      - Assigns to spatial region for distance calculations
 *
 *   5. Set login_time
 *      - Records current Unix timestamp
 *      - Used for calculating session duration
 *      - May be used for anti-AFK timeout
 *
 *   6. Log confirmation message
 *      - Prints to stdout for debugging
 *
 * UPDATE FLAGS
 * ------------
 * The update_flags field is a bitfield that indicates which player attributes
 * have changed and need to be synchronized to nearby players.
 *
 *   Bit | Flag Name           | Description
 *   ----+---------------------+--------------------------------------------
 *   0   | UPDATE_APPEARANCE   | Look, equipment, combat level changed
 *   1   | UPDATE_ANIMATION    | Playing an animation (walk, run, etc.)
 *   2   | UPDATE_GRAPHICS     | Spotanim (spell graphics, teleport, etc.)
 *   3   | UPDATE_FORCED_CHAT  | Text bubble above head
 *   4   | UPDATE_CHAT         | Public chat message
 *   5   | UPDATE_INTERACTING  | Following/attacking another entity
 *   6   | UPDATE_HIT          | Taking damage (hitmark/healthbar)
 *   7   | UPDATE_FORCED_MOVE  | Teleport/knockback movement
 *
 *   Setting UPDATE_APPEARANCE (0x1) on login ensures other players see:
 *     - Gender, body colors, hairstyle, clothing
 *     - Equipped items (weapons, armor, cape, etc.)
 *     - Skill level (combat level calculation)
 *     - Player name, title
 *
 * REGION SYSTEM
 * -------------
 * The game world is divided into regions for efficient spatial partitioning.
 *
 *   Region Size: 64x64 tiles (8x8 chunks, 1 chunk = 8x8 tiles)
 *   Region Coordinates: (player_x / 64, player_y / 64)
 *
 *   When needs_placement is set:
 *     1. Server sends player absolute position
 *     2. Client sets viewport center
 *     3. Client requests map chunks for surrounding regions
 *
 *   When region_changed is set:
 *     1. Server sends map data (terrain, objects, walls)
 *     2. Client rebuilds local cache
 *     3. Client renders visible area
 *
 *   Typical region load sends:
 *     - Landscape data (height, texture, overlay)
 *     - Static objects (trees, rocks, buildings)
 *     - Interactive objects (doors, chests, ladders)
 *     - NPCs in region
 *     - Other players in region
 *
 * WORLD REGISTRATION
 * ------------------
 * The world maintains several data structures for player management:
 *
 *   1. Player Array (indexed by slot):
 *      - g_world->players[0..2047]
 *      - Fast lookup by player index
 *      - Used for update iteration
 *
 *   2. Username Hash Map:
 *      - g_world->player_map["username"] = player
 *      - Fast lookup for private messages, trading
 *      - Used for friends list online status
 *
 *   3. Spatial Index (region-based):
 *      - g_world->regions[region_x][region_y] = player_list
 *      - Fast lookup of nearby players
 *      - Used for visibility, combat, chat range
 *
 *   Registration Process:
 *     world_register_player(world, player, username):
 *       1. Find empty slot in players array
 *       2. Add player to username map
 *       3. Add player to spatial index for current region
 *       4. Broadcast "friend logged in" to friends list
 *
 * LOGIN TIME
 * ----------
 * The login_time field stores Unix timestamp (seconds since epoch).
 *
 *   Usage:
 *     - Session duration: current_time - login_time
 *     - AFK detection: If no input for 5 minutes, auto-logout
 *     - Statistics: Track peak hours, average session length
 *
 *   Example:
 *     login_time = 1699999999 (2023-11-15 00:59:59 UTC)
 *     current_time = 1700000599 (2023-11-15 01:09:59 UTC)
 *     session_duration = 600 seconds (10 minutes)
 *
 * PARAMETERS
 * ----------
 *   player : Pointer to authenticated Player structure
 *            - Must have state = PLAYER_STATE_LOGGED_IN
 *            - Must have valid username
 *            - Must have valid position (x, y)
 *
 * RETURN VALUE
 * ------------
 *   void (no return value)
 *
 * SIDE EFFECTS
 * ------------
 *   - Sets player->needs_placement = true
 *   - Sets player->region_changed = true
 *   - Sets player->update_flags = 0x1 (UPDATE_APPEARANCE)
 *   - Registers player with g_world (if not NULL)
 *   - Sets player->login_time to current Unix timestamp
 *   - Prints confirmation message to stdout
 *
 * EXAMPLE
 * -------
 *   if (login_process_header(player, in)) {
 *       // Login successful, finalize
 *       login_send_initial_packets(player);
 *       
 *       // Player now in game loop
 *       while (player->state == PLAYER_STATE_LOGGED_IN) {
 *           server_process_player_packets(player);
 *           server_update_player(player);
 *       }
 *   }
 *
 * TIME COMPLEXITY
 * ---------------
 *   O(1) - All operations are constant time:
 *     - Flag assignments: O(1)
 *     - world_register_player: O(1) average (hash map insert)
 *     - time(NULL): O(1) system call
 *
 * SPACE COMPLEXITY
 * ----------------
 *   O(1) - No allocations, only modifies existing player structure
 */
void login_send_initial_packets(Player* player) {
    /* 
     * Note: For protocol 225, we don't send any special packets here.
     * The actual game initialization packets (player position, sidebar,
     * map regions, etc.) are sent by server_send_initial_game_packets().
     * 
     * This function only sets up the internal state flags.
     */
    
    /* 
     * Set needs_placement flag.
     * This triggers the server to send the player's absolute position
     * in the next game tick. The client uses this to initialize the
     * viewport and request map data.
     */
    player->needs_placement = true;
    
    /* 
     * Set region_changed flag.
     * This triggers the server to send map chunk data for the player's
     * current region. The client needs this to render the visible world.
     */
    player->region_changed = true;
    
    /* 
     * Set UPDATE_APPEARANCE flag (bit 0).
     * This ensures other players receive this player's appearance data
     * in the next player update cycle. Without this, the player would
     * be invisible to others.
     */
    player->update_flags = 0x1;  /* UPDATE_APPEARANCE flag */
    
    /* 
     * Register player with world.
     * This adds the player to global tracking structures:
     *   - Player array for iteration
     *   - Username map for lookups (friends, trading, PMs)
     *   - Spatial index for nearby player queries
     * 
     * If g_world is NULL (server not fully initialized), skip registration.
     */
    if (g_world) {
        world_register_player(g_world, player, player->username);
    }
    
    /* 
     * Set login time to current Unix timestamp.
     * Used for:
     *   - Session duration tracking
     *   - AFK timeout detection
     *   - Player statistics (peak hours, retention)
     */
    player->login_time = (u64)time(NULL);
    
    /* Log successful setup for debugging */
    printf("Player setup complete for %s\n", player->username);
}
