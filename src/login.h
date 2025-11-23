/*
 * ==============================================================================
 * login.h - RuneScape Login Protocol Interface
 * ==============================================================================
 *
 * PURPOSE
 * -------
 * This header defines the interface for the RuneScape login protocol handler,
 * which manages the complete authentication handshake between client and server.
 * The login process is stateful and proceeds through three distinct stages to
 * establish a secure, authenticated connection.
 *
 * PROTOCOL OVERVIEW
 * -----------------
 * The RuneScape login protocol (revision 225) follows a three-stage handshake:
 *
 *   Stage 1: CONNECTION
 *   -------------------
 *   Server sends two random 32-bit seeds to the client for ISAAC cipher
 *   initialization. These seeds establish the cryptographic session.
 *
 *        Client                          Server
 *          |                               |
 *          |------ TCP Connection -------->|
 *          |                               |
 *          |<------- Server Seeds ---------|
 *          |      [seed1, seed2]           |
 *          |      (8 bytes total)          |
 *
 *   Stage 2: HEADER
 *   ---------------
 *   Client sends login request containing credentials, version info, and
 *   ISAAC cipher seeds. Server validates and responds with login status.
 *
 *          |                               |
 *          |------- Login Header --------->|
 *          | - Login type (16 or 18)       |
 *          | - Client version (225)        |
 *          | - CRC checksums (9x u32)      |
 *          | - RSA encrypted block:        |
 *          |   * Client ISAAC seeds (4x)   |
 *          |   * Username                  |
 *          |   * Password                  |
 *          |                               |
 *          |<---- Login Response Code -----|
 *          |      (1 byte status)          |
 *
 *   Stage 3: PAYLOAD (Future Use)
 *   ------------------------------
 *   Reserved for additional post-authentication data exchange.
 *
 * LOGIN RESPONSE CODES
 * --------------------
 * The server responds with a single byte status code:
 *
 *   Code | Constant                         | Meaning
 *   -----+----------------------------------+--------------------------------
 *     2  | LOGIN_RESPONSE_OK                | Success - player logged in
 *     3  | LOGIN_RESPONSE_INVALID_CREDENTIALS| Username/password incorrect
 *     5  | LOGIN_RESPONSE_ACCOUNT_ONLINE    | Account already logged in
 *    15  | LOGIN_RESPONSE_RECONNECT         | Reconnection request
 *    18  | LOGIN_RESPONSE_SUCCESS_STAFF     | Staff member login success
 *
 * ISAAC CIPHER INITIALIZATION
 * ---------------------------
 * ISAAC (Indirection, Shift, Accumulate, Add, and Count) is a cryptographically
 * secure PRNG used to obfuscate packet opcodes. The cipher requires initialization
 * with seed values:
 *
 *   Seed Source:
 *   -----------
 *   - Client generates 4x 32-bit seeds (16 bytes total)
 *   - Client sends seeds to server in RSA-encrypted block
 *   - Server derives two cipher instances:
 *     * Incoming cipher: seeds = [S0, S1, S2, S3]
 *     * Outgoing cipher: seeds = [S0+50, S1+50, S2+50, S3+50]
 *
 *   Purpose:
 *   --------
 *   Each packet opcode is XORed with next ISAAC output to prevent:
 *   - Packet type identification by network observers
 *   - Replay attacks (different opcode encoding each time)
 *   - Pattern analysis of communication
 *
 * SECURITY CONSIDERATIONS
 * -----------------------
 * 1. Timing Attacks:
 *    - Password validation should use constant-time comparison
 *    - Current implementation may leak information via timing
 *
 * 2. Password Storage:
 *    - Passwords transmitted in plaintext (within RSA block)
 *    - Server should hash passwords with salt (bcrypt/argon2)
 *    - Current implementation stores plaintext (INSECURE)
 *
 * 3. RSA Encryption:
 *    - Client encrypts sensitive data with server public key
 *    - Server decrypts with private key
 *    - Current implementation skips RSA for simplicity
 *
 * 4. Seed Security:
 *    - Server seeds must be cryptographically random
 *    - Current implementation uses rand() (NOT cryptographically secure)
 *    - Production should use /dev/urandom or equivalent
 *
 * STATE MACHINE
 * -------------
 * The login process follows this state transition diagram:
 *
 *   [AWAITING_CONNECTION]
 *           |
 *           | Server sends seeds
 *           v
 *   [AWAITING_HEADER]
 *           |
 *           | Client sends credentials
 *           | Server validates
 *           v
 *   [AWAITING_PAYLOAD] (currently unused)
 *           |
 *           | Authentication complete
 *           v
 *   Player state -> PLAYER_STATE_LOGGED_IN
 *
 * TYPICAL USAGE
 * -------------
 *   Player* player = player_create(socket_fd);
 *   
 *   // Stage 1: Send server seeds
 *   if (!login_process_connection(player)) {
 *       // Failed to send seeds
 *       player_destroy(player);
 *       return;
 *   }
 *   
 *   // Stage 2: Process login header when data arrives
 *   StreamBuffer* in = buffer_create_from_socket(player->socket_fd);
 *   if (!login_process_header(player, in)) {
 *       // Invalid credentials or protocol error
 *       buffer_destroy(in);
 *       player_destroy(player);
 *       return;
 *   }
 *   buffer_destroy(in);
 *   
 *   // Stage 3: Send initial game packets
 *   login_send_initial_packets(player);
 *   
 *   // Player now ready for normal game protocol
 *
 * CROSS-REFERENCES
 * ----------------
 * - buffer.h/buffer.c : Stream buffer I/O operations
 * - network.h/network.c : Socket send/receive primitives
 * - player.h/player.c : Player state management
 * - isaac.h/isaac.c : ISAAC cipher implementation
 * - world.h/world.c : Player registration in game world
 *
 * PROTOCOL SPECIFICATION
 * ----------------------
 * Client Version: 225 (RuneScape 2, circa 2004)
 * Login Types:
 *   - 16: Normal login
 *   - 18: Reconnect login
 * Packet Structure: See individual function documentation in login.c
 *
 * ==============================================================================
 */

#ifndef LOGIN_H
#define LOGIN_H

#include "types.h"
#include "player.h"
#include "buffer.h"

/* 
 * Login Response Codes
 * --------------------
 * These single-byte values are sent from server to client after processing
 * the login request. The client uses these codes to display appropriate
 * messages to the user.
 */

/* Login successful - player authenticated and ready for game */
#define LOGIN_RESPONSE_OK 2

/* Invalid username or password */
#define LOGIN_RESPONSE_INVALID_CREDENTIALS 3

/* Account is already logged in (prevents duplicate sessions) */
#define LOGIN_RESPONSE_ACCOUNT_ONLINE 5

/* Reconnection acknowledged (for session recovery) */
#define LOGIN_RESPONSE_RECONNECT 15

/* Successful login for staff member (may trigger special client behavior) */
#define LOGIN_RESPONSE_SUCCESS_STAFF 18

/*
 * Login Stage Enumeration
 * ------------------------
 * Represents the current stage of the login handshake. The login process
 * is stateful and must proceed sequentially through these stages.
 *
 * Stage Progression:
 *   AWAITING_CONNECTION -> AWAITING_HEADER -> AWAITING_PAYLOAD -> LOGGED_IN
 *
 * Each stage has specific data requirements and validation rules.
 */
typedef enum {
    /* Initial state - server must send ISAAC seeds to client */
    LOGIN_STAGE_AWAITING_CONNECTION,
    
    /* Seeds sent - server awaits login header with credentials */
    LOGIN_STAGE_AWAITING_HEADER,
    
    /* Header processed - server awaits additional payload (currently unused) */
    LOGIN_STAGE_AWAITING_PAYLOAD
} LoginStage;

/*
 * ==============================================================================
 * FUNCTION PROTOTYPES
 * ==============================================================================
 */

/*
 * login_process_connection - Stage 1: Send server ISAAC seeds
 * ------------------------------------------------------------
 * Generates and sends two random 32-bit seeds to the client for ISAAC cipher
 * initialization. This is the first step in the login handshake.
 *
 * Parameters:
 *   player : Pointer to player structure containing socket file descriptor
 *
 * Returns:
 *   true  : Seeds successfully sent to client
 *   false : Network error occurred, connection should be closed
 *
 * Side Effects:
 *   - Sends 8 bytes (2x u32 big-endian) to client socket
 *   - Does NOT store seeds (client will send derived seeds back)
 *   - Prints debug message with player index
 *
 * Security Note:
 *   Current implementation uses rand() which is NOT cryptographically secure.
 *   Production code should use a CSPRNG like /dev/urandom.
 *
 * Example:
 *   Player* player = player_create(client_socket);
 *   if (!login_process_connection(player)) {
 *       fprintf(stderr, "Failed to send seeds to client\n");
 *       player_destroy(player);
 *   }
 */
bool login_process_connection(Player* player);

/*
 * login_process_header - Stage 2: Process login credentials
 * ----------------------------------------------------------
 * Reads and validates the client login request, including protocol version,
 * CRC checksums, ISAAC seeds, and credentials. Initializes ISAAC ciphers
 * and sends login response code.
 *
 * Parameters:
 *   player : Pointer to player structure (modified with credentials/ciphers)
 *   in     : Input stream buffer containing login header data
 *
 * Returns:
 *   true  : Login successful, player authenticated
 *   false : Login failed (invalid version, bad data, or network error)
 *
 * Side Effects:
 *   - Reads login type, version, CRC checksums, ISAAC seeds, credentials
 *   - Initializes player->in_cipher and player->out_cipher
 *   - Stores username and password in player structure
 *   - Sets player->state to PLAYER_STATE_LOGGED_IN on success
 *   - Sends LOGIN_RESPONSE_OK (or error code) to client
 *   - Prints debug messages for seeds, credentials, and login status
 *
 * Protocol Details:
 *   Login Header Structure (big-endian):
 *     Byte(s) | Type | Description
 *     --------+------+--------------------------------------------------
 *     0       | u8   | Login type (16=normal, 18=reconnect)
 *     1       | u8   | Block length (remaining bytes in login block)
 *     2       | u8   | Client version (must be 225)
 *     3       | u8   | High/low memory flag
 *     4-39    | u32  | 9x CRC32 checksums for cache files
 *     40      | u8   | RSA block length
 *     41      | u8   | RSA opcode (always 10)
 *     42-57   | u32  | 4x ISAAC seeds (client-generated)
 *     58-61   | u32  | UID (unique identifier)
 *     62-...  | str  | Username (terminated by byte 10)
 *     ...-... | str  | Password (terminated by byte 10)
 *
 *   ISAAC Cipher Initialization:
 *     in_cipher  : Initialized with client seeds [S0, S1, S2, S3]
 *     out_cipher : Initialized with seeds [S0+50, S1+50, S2+50, S3+50]
 *
 * Example:
 *   StreamBuffer* in = buffer_create(512);
 *   network_receive(player->socket_fd, in);
 *   if (login_process_header(player, in)) {
 *       printf("Player %s logged in\n", player->username);
 *   } else {
 *       printf("Login failed\n");
 *   }
 *   buffer_destroy(in);
 */
bool login_process_header(Player* player, StreamBuffer* in);

/*
 * login_process_payload - Stage 3: Process additional login data
 * ---------------------------------------------------------------
 * Reserved for processing additional payload data after login header.
 * Currently unused in protocol 225.
 *
 * Parameters:
 *   player : Pointer to player structure
 *   in     : Input stream buffer containing payload data
 *
 * Returns:
 *   true : Always (no payload processing implemented)
 *
 * Future Use:
 *   Could be used for:
 *   - Character selection (if multiple characters per account)
 *   - Client settings synchronization
 *   - Anti-cheat data submission
 */
bool login_process_payload(Player* player, StreamBuffer* in);

/*
 * login_send_initial_packets - Finalize login and prepare player for game
 * ------------------------------------------------------------------------
 * Performs post-authentication initialization, including registering the
 * player with the game world and setting flags for initial updates.
 *
 * Parameters:
 *   player : Pointer to authenticated player (state must be LOGGED_IN)
 *
 * Returns:
 *   void
 *
 * Side Effects:
 *   - Sets player->needs_placement = true (triggers region load)
 *   - Sets player->region_changed = true (sends map chunks)
 *   - Sets player->update_flags = 0x1 (UPDATE_APPEARANCE flag)
 *   - Registers player with g_world using username as key
 *   - Sets player->login_time to current Unix timestamp
 *   - Prints confirmation message
 *
 * Notes:
 *   - Does NOT send game packets (handled by server_send_initial_game_packets)
 *   - Must be called after successful login_process_header()
 *   - Player will appear in game world after next update cycle
 *
 * Example:
 *   if (login_process_header(player, in)) {
 *       login_send_initial_packets(player);
 *       // Player now ready for game loop
 *   }
 */
void login_send_initial_packets(Player* player);

#endif /* LOGIN_H */
