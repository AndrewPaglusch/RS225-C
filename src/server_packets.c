/*******************************************************************************
 * SERVER_PACKETS.C - Outgoing Packet Construction (Server → Client)
 *******************************************************************************
 * 
 * This file implements all server-to-client packet builders for the RuneScape
 * protocol revision 225 (May 2004). Each function constructs a properly
 * formatted packet and transmits it over the network.
 * 
 * KEY ALGORITHMS IMPLEMENTED:
 *   1. Packet framing (fixed, VAR_BYTE, VAR_SHORT)
 *   2. ISAAC cipher integration for opcode encryption
 *   3. Interface state synchronization
 *   4. Inventory/equipment updates
 *   5. Player configuration (stats, varps, interfaces)
 * 
 * EDUCATIONAL CONCEPTS:
 *   - Network protocol design
 *   - Packet serialization
 *   - State synchronization
 *   - Stream encryption
 *   - Endianness handling
 * 
 * PACKET FRAMING TYPES:
 * 
 * RuneScape uses three packet frame types:
 * 
 * 1. FIXED LENGTH:
 *    Structure: [encrypted_opcode][payload]
 *    Size:      Known at compile time
 *    Example:   UPDATE_STAT (opcode 44, 6 bytes payload)
 *    
 *    Wire format:
 *    ┌──────────┬──────────┬──────────┬──────────┐
 *    │ opcode   │ skill_id │  level   │   xp     │
 *    │ (1 byte) │ (1 byte) │ (1 byte) │ (4 bytes)│
 *    └──────────┴──────────┴──────────┴──────────┘
 * 
 * 2. VAR_BYTE (Variable byte length):
 *    Structure: [encrypted_opcode][1-byte length][payload]
 *    Size:      0-255 bytes
 *    Example:   MESSAGE_GAME (opcode 4)
 *    
 *    Wire format:
 *    ┌──────────┬──────────┬─────────────────────┐
 *    │ opcode   │ length   │  payload (string)   │
 *    │ (1 byte) │ (1 byte) │  (0-255 bytes)      │
 *    └──────────┴──────────┴─────────────────────┘
 *    
 *    Length field: Payload size only (excludes opcode and length byte)
 * 
 * 3. VAR_SHORT (Variable short length):
 *    Structure: [encrypted_opcode][2-byte length][payload]
 *    Size:      0-65535 bytes
 *    Example:   UPDATE_INV_FULL (opcode 98)
 *    
 *    Wire format:
 *    ┌──────────┬───────────────────┬─────────────────────┐
 *    │ opcode   │    length         │  payload (items)    │
 *    │ (1 byte) │ (2 bytes, big-E)  │  (0-65535 bytes)    │
 *    └──────────┴───────────────────┴─────────────────────┘
 *    
 *    Length field: Big-endian 16-bit (MSB first)
 * 
 * ISAAC CIPHER ENCRYPTION:
 * 
 * All packet opcodes are encrypted with ISAAC cipher after login:
 * 
 * Encryption process:
 *   1. Get next ISAAC random value: key = isaac_get_next(cipher)
 *   2. Encrypt opcode: encrypted = (opcode + key) & 0xFF
 *   3. Write encrypted opcode to buffer
 * 
 * Client decryption (mirror process):
 *   1. Read encrypted opcode from socket
 *   2. Get next ISAAC random value: key = isaac_get_next(cipher)
 *   3. Decrypt opcode: opcode = (encrypted - key) & 0xFF
 * 
 * CRITICAL: Both sides must call isaac_get_next() in perfect lockstep!
 *   - If server sends packet without encrypting, desync occurs
 *   - If server skips a packet, all future opcodes decrypt incorrectly
 *   - Login handshake must initialize ciphers identically
 * 
 * Example synchronization:
 *   Server sends:                    Client receives:
 *   1. MESSAGE_GAME (enc w/ key[0])  1. Decrypt with key[0]
 *   2. UPDATE_STAT (enc w/ key[1])   2. Decrypt with key[1]
 *   3. UPDATE_INV_FULL (enc w/ key[2]) 3. Decrypt with key[2]
 * 
 * PACKET CONSTRUCTION PATTERN:
 * 
 * All packet functions follow this structure:
 * 
 * void send_PACKET_NAME(Player* player, ...) {
 *     // 1. Null check
 *     if (!player) return;
 *     
 *     // 2. Get cipher (if initialized)
 *     ISAACCipher* enc = enc_for(player);
 *     
 *     // 3. Allocate buffer (estimate size)
 *     StreamBuffer* out = buffer_create(estimated_size);
 *     
 *     // 4. Write header (encrypted opcode + length if variable)
 *     buffer_write_header_var(out, OPCODE, enc, VAR_TYPE);
 *     u32 payload_start = out->position;
 *     
 *     // 5. Write payload data
 *     buffer_write_byte(out, value1);
 *     buffer_write_short(out, value2, BYTE_ORDER_BIG);
 *     // ...
 *     
 *     // 6. Finalize header (backfill length if variable)
 *     buffer_finish_var_header(out, VAR_TYPE);
 *     
 *     // 7. Debug logging
 *     dbg_log_send("PACKET_NAME", OPCODE, "frame_type", payload_len, encrypted);
 *     
 *     // 8. Transmit to client
 *     network_send(player->socket_fd, out->data, out->position);
 *     
 *     // 9. Cleanup
 *     buffer_destroy(out);
 * }
 * 
 * BUFFER SIZE ESTIMATION:
 * 
 * Why estimate buffer size?
 *   - Reduces realloc() calls (better performance)
 *   - Prevents memory fragmentation
 *   - Allocation overhead is O(1) if size is correct
 * 
 * Estimation formula:
 *   size = header_size + payload_size + safety_margin
 *   
 *   Fixed:     1 + payload
 *   VAR_BYTE:  2 + payload
 *   VAR_SHORT: 3 + payload
 *   
 *   Safety margin: Add 8-16 bytes for worst-case alignment
 * 
 * Example:
 *   MESSAGE_GAME with 50-char message:
 *   size = 2 (header) + 50 (string) + 1 (terminator) + 8 (margin) = 61 bytes
 *   Round up to power of 2: 64 bytes
 * 
 * VARIABLE-LENGTH HEADER BACKFILLING:
 * 
 * Problem: Length field must contain payload size, but we don't know
 *          the size until after writing the payload!
 * 
 * Solution: Two-pass encoding
 * 
 * Pass 1 - Write placeholder:
 *   buffer_write_header_var(out, opcode, cipher, VAR_BYTE);
 *     → Writes encrypted opcode
 *     → Writes 0x00 as length placeholder
 *     → Remembers position of length field (var_len_pos)
 *   
 * Write payload:
 *   buffer_write_string(out, "Hello");
 *     → position advances by 6 bytes
 *   
 * Pass 2 - Backfill length:
 *   buffer_finish_var_header(out, VAR_BYTE);
 *     → Calculates payload_len = current_pos - (var_len_pos + 1)
 *     → Overwrites placeholder at var_len_pos with actual length
 * 
 * Memory layout evolution:
 * 
 * Initial (after write_header_var):
 *   [encrypted_opcode][0x00][          ]
 *                       ↑
 *                   placeholder
 * 
 * After writing payload:
 *   [encrypted_opcode][0x00][H][e][l][l][o][0x0A]
 *                       ↑     ←─────6 bytes─────→
 *                   placeholder
 * 
 * After finish_var_header:
 *   [encrypted_opcode][0x06][H][e][l][l][o][0x0A]
 *                       ↑
 *                   backfilled with 6
 * 
 * INTERFACE SYSTEM:
 * 
 * RuneScape's UI is hierarchical:
 * 
 * Root interface (viewport):
 *   └─ Sidebar tabs (0-13)
 *       ├─ Tab 0: Combat styles (interface 5855)
 *       ├─ Tab 1: Stats (interface 3917)
 *       ├─ Tab 3: Inventory (interface 3213)
 *       └─ ...
 * 
 * Interface operations:
 *   IF_OPENTOP:   Set root interface (replaces entire screen)
 *   IF_SETTAB:    Assign interface to sidebar tab slot
 *   IF_SETTEXT:   Update text label within interface
 *   IF_SETHIDE:   Show/hide interface component
 * 
 * Interface IDs are hardcoded in client cache:
 *   3559: Main game viewport
 *   3213: Inventory container
 *   1644: Equipment screen
 *   5855: Combat styles tab
 * 
 * VARP SYSTEM (Client Variables):
 * 
 * VARPs (Variable Parameters) are client-side state variables
 * Specific VARP IDs and their meanings defined in client cache
 * 
 * Two packet types:
 *   VARP_SMALL (opcode 150): 1-byte value (0-127)
 *   VARP_LARGE (opcode 175): 4-byte value (any i32)
 * 
 * Client synchronization:
 *   Server sends VARP packet with ID and value
 *   Client receives packet and updates internal VARP array
 *   Client re-renders affected UI elements
 * 
 * DEBUG LOGGING:
 * 
 * dbg_log_send() is defined in packets.h (if DEBUG_PACKETS enabled):
 * 
 * #ifdef DEBUG_PACKETS
 * #define dbg_log_send(name, opc, type, len, enc) \
 *     printf("[TX] %s op=%u type=%s len=%d enc=%d\n", name, opc, type, len, enc)
 * #else
 * #define dbg_log_send(...)  // No-op
 * #endif
 * 
 * Output example:
 *   [TX] MESSAGE_GAME op=4 type=varbyte len=15 enc=1
 *   [TX] UPDATE_STAT op=44 type=fixed len=6 enc=1
 * 
 * Useful for:
 *   - Protocol debugging
 *   - Verifying packet sizes
 *   - Detecting missing encryption
 * 
 * COMPLEXITY ANALYSIS:
 *   Most functions: O(1) time, O(1) space
 *   send_player_stats(): O(N) where N = skill count (23)
 *   send_interfaces(): O(N) where N = tab count (13)
 * 
 ******************************************************************************/

#include "server_packets.h"  /* brings in types/player + SERVER_* ids */
#include "buffer.h"
#include "network.h"
#include "server.h"
#include <string.h>

/*******************************************************************************
 * HELPER FUNCTIONS
 ******************************************************************************/

/*
 * enc_for - Get ISAAC cipher for player (if initialized)
 * 
 * @param p  Player pointer
 * @return   Pointer to out_cipher if initialized, NULL otherwise
 * 
 * PURPOSE:
 *   Provides safe access to encryption cipher
 *   Returns NULL if player hasn't completed login (no encryption yet)
 * 
 * USAGE:
 *   ISAACCipher* enc = enc_for(player);
 *   buffer_write_header(out, opcode, enc);  // Encrypts only if enc != NULL
 * 
 * COMPLEXITY: O(1)
 */
static inline ISAACCipher* enc_for(Player* p) {
    return (p && p->out_cipher.initialized) ? &p->out_cipher : NULL;
}

/*******************************************************************************
 * MESSAGE PACKETS
 ******************************************************************************/

/* MESSAGE_GAME (4): VAR_BYTE + string */

/*
 * send_player_message - Send chat message to player
 * 
 * @param player  Target player
 * @param msg     Message text (null-terminated C string)
 * 
 * PACKET STRUCTURE:
 *   Opcode: 4 (MESSAGE_GAME)
 *   Type:   VAR_BYTE
 *   Payload: [string] (newline-terminated)
 * 
 * WIRE FORMAT:
 *   ┌──────────┬──────────┬─────────────────────┬──────────┐
 *   │ opcode   │ length   │  message chars      │  0x0A    │
 *   │ (1 byte) │ (1 byte) │  (N bytes)          │ (1 byte) │
 *   └──────────┴──────────┴─────────────────────┴──────────┘
 * 
 * STRING ENCODING:
 *   RuneScape uses newline (0x0A) as string terminator
 *   Standard C uses null (0x00) terminator
 *   buffer_write_string() handles conversion
 * 
 * MESSAGE TYPES:
 *   Different opcodes for different message types:
 *   - 4:  Game message (yellow text)
 *   - 41: Private message (cyan text)
 *   - 70: Public chat (white text)
 * 
 * EXAMPLE:
 *   send_player_message(player, "Welcome to RuneScape!");
 *   
 *   Wire bytes: [0xXX][0x16][W][e][l][c][o][m][e]...[!][0x0A]
 *               opcode len=22 (21 chars + terminator)
 * 
 * BUFFER SIZE CALCULATION:
 *   Base:    3 bytes (opcode + length + terminator)
 *   String:  strlen(msg) bytes
 *   Margin:  2 bytes (safety)
 *   Total:   3 + strlen(msg) + 2
 * 
 * COMPLEXITY: O(N) where N = message length
 */
void send_player_message(Player* player, const char* msg) {
    if (!player || !msg) return;
    ISAACCipher* enc = enc_for(player);

    StreamBuffer* out = buffer_create(3u + (u32)strlen(msg) + 2u);
    buffer_write_header_var(out, SERVER_MESSAGE_GAME, enc, VAR_BYTE);
    u32 payload_start = out->position;

    buffer_write_string(out, msg);

    buffer_finish_var_header(out, VAR_BYTE);
    dbg_log_send("MESSAGE_GAME", SERVER_MESSAGE_GAME, "varbyte", (int)(out->position - payload_start), enc != NULL);

    network_send(player->socket_fd, out->data, out->position);
    buffer_destroy(out);
}

/*******************************************************************************
 * PLAYER STATE PACKETS
 ******************************************************************************/

/* 44: UPDATE_STAT — payload = 1(skill) + 1(level) + 4(xp) = 6 bytes */

/*
 * send_player_stats - Send all 23 skill levels and XP values
 * 
 * @param player  Target player
 * 
 * PACKET STRUCTURE:
 *   Opcode: 44 (UPDATE_STAT)
 *   Type:   Fixed (6 bytes payload)
 *   Payload: [skill_id:1][level:1][xp:4]
 * 
 * SKILLS:
 *   Game has 23 skills (IDs 0-22)
 *   Specific skill names and order defined in client cache
 * 
 * ALGORITHM:
 *   For each skill (0 to 22):
 *     1. Create packet buffer (7 bytes: 1 header + 6 payload)
 *     2. Write encrypted opcode
 *     3. Write skill ID (1 byte)
 *     4. Write level (1 byte, typically 1-99)
 *     5. Write XP (4 bytes big-endian, typically 0 to 200M)
 *     6. Send packet
 *     7. Free buffer
 * 
 * WHY SEPARATE PACKETS?
 *   Could send all stats in one packet, but:
 *   - Easier to update individual skills
 *   - Client processes incrementally
 *   - Reduces complexity
 * 
 * WIRE FORMAT (per skill):
 *   ┌──────────┬──────────┬──────────┬───────────────────────┐
 *   │ opcode   │ skill_id │  level   │     experience        │
 *   │ (1 byte) │ (1 byte) │ (1 byte) │ (4 bytes, big-endian) │
 *   └──────────┴──────────┴──────────┴───────────────────────┘
 * 
 * EXPERIENCE VALUES:
 *   XP values are 4-byte big-endian integers
 *   Experience to level mapping defined by client
 * 
 * DEFAULTS:
 *   Level 1 for all skills = fresh account
 *   XP 0 for all skills = no progress
 * 
 * TOTAL BANDWIDTH:
 *   23 skills * 7 bytes = 161 bytes
 *   Sent once per login
 * 
 * COMPLEXITY: O(N) where N = skill count (23)
 */
void send_player_stats(Player* player) {
    if (!player) return;

    const i32 SKILL_COUNT   = 21;  /* Changed from 23 to match player.h SKILL_COUNT */

    printf("DEBUG: Sending player stats for '%s'\n", player->username);
    
    for (i32 skill = 0; skill < SKILL_COUNT; skill++) {
        /* total = 1(opcode) + 6(payload) = 7 bytes */
        StreamBuffer* out = buffer_create(7);

        buffer_write_header(out, SERVER_UPDATE_STAT,
                            player->out_cipher.initialized ? &player->out_cipher : NULL);

        u32 payload_start = buffer_get_position(out);

        /* Read actual player data from levels[] and experience[] arrays */
        u8 level = player->levels[skill];
        u32 xp = player->experience[skill];

        if (skill == 3) {  /* Hitpoints */
            printf("DEBUG:   Skill %d (HP): level=%u, xp=%u\n", skill, level, xp);
        }

        buffer_write_byte(out, (u8)skill);                 /* skill id      */
        buffer_write_int(out, xp / 10, BYTE_ORDER_BIG);    /* experience / 10 */
        buffer_write_byte(out, level);                     /* current level */

        int payload_len = (int)(buffer_get_position(out) - payload_start);
        dbg_log_send("UPDATE_STAT", SERVER_UPDATE_STAT, "fixed",
                     payload_len, player->out_cipher.initialized ? 1 : 0);

        network_send(player->socket_fd, out->data, out->position);
        buffer_destroy(out);
    }
}

/*******************************************************************************
 * INVENTORY PACKETS
 ******************************************************************************/

/* 98: UPDATE_INV_FULL (varshort) – inventory (component 3214) */

/*
 * send_inventory - Send inventory contents to client
 * 
 * @param player  Target player
 * 
 * PACKET STRUCTURE:
 *   Opcode: 98 (UPDATE_INV_FULL)
 *   Type:   VAR_SHORT
 *   Payload: [interface_id:2][item_count:1][items...]
 * 
 * INVENTORY INTERFACE:
 *   Interface 3214: Inventory container (component within interface 3213)
 *   Contains 28 item slots
 * 
 * ITEM ENCODING (not implemented here, sends empty):
 *   For each item:
 *     [item_id:2][item_count:1 or 4]
 *   
 *   If count < 255: 1 byte
 *   If count >= 255: 1 byte 0xFF + 4 bytes actual count
 * 
 * EMPTY INVENTORY:
 *   item_count = 0
 *   No item data follows
 * 
 * WIRE FORMAT (empty):
 *   ┌──────────┬───────────────┬──────────────────┬────────────┐
 *   │ opcode   │    length     │  interface_id    │ item_count │
 *   │ (1 byte) │ (2 bytes, BE) │  (2 bytes, BE)   │  (1 byte)  │
 *   └──────────┴───────────────┴──────────────────┴────────────┘
 *   
 *   Example: [0xXX][0x00][0x03][0x0C][0x8E][0x00]
 *            opcode len=3    id=3214    count=0
 * 
 * COMPLEXITY: O(1) for empty, O(N) for N items
 */
void send_inventory(Player* player) {
    if (!player) return;

    StreamBuffer* out = buffer_create(8);
    buffer_write_header_var(out, SERVER_UPDATE_INV_FULL,
                            player->out_cipher.initialized ? &player->out_cipher : NULL,
                            VAR_SHORT);

    u32 payload_start = buffer_get_position(out);

    buffer_write_short(out, 3214, BYTE_ORDER_BIG);  /* inventory interface/component */
    buffer_write_byte(out, 0);                      /* item count (0 for now)        */

    buffer_finish_var_header(out, VAR_SHORT);

    int payload_len = (int)(buffer_get_position(out) - payload_start);
    dbg_log_send("UPDATE_INV_FULL(inv)", SERVER_UPDATE_INV_FULL, "varshort",
                 payload_len, player->out_cipher.initialized ? 1 : 0);

    network_send(player->socket_fd, out->data, out->position);
    buffer_destroy(out);
}

/* 98: UPDATE_INV_FULL (varshort) – equipment (component 1688) */

/*
 * send_equipment - Send equipment contents to client
 * 
 * @param player  Target player
 * 
 * EQUIPMENT INTERFACE:
 *   Interface 1688: Equipment container
 *   Contains 14 equipment slots:
 *     0: Head       5: Body       10: Ring
 *     1: Cape       6: Shield     11: Ammo
 *     2: Amulet     7: Legs       12: Aura
 *     3: Weapon     8: Gloves     13: Pocket
 *     4: Chest      9: Boots
 * 
 * Same encoding as inventory, but different interface ID
 * 
 * COMPLEXITY: O(1) for empty, O(N) for N items
 */
void send_equipment(Player* player) {
    if (!player) return;

    StreamBuffer* out = buffer_create(8);
    buffer_write_header_var(out, SERVER_UPDATE_INV_FULL,
                            player->out_cipher.initialized ? &player->out_cipher : NULL,
                            VAR_SHORT);

    u32 payload_start = buffer_get_position(out);

    buffer_write_short(out, 1688, BYTE_ORDER_BIG);  /* equipment interface/component */
    buffer_write_byte(out, 0);                      /* item count (0 for now)        */

    buffer_finish_var_header(out, VAR_SHORT);

    int payload_len = (int)(buffer_get_position(out) - payload_start);
    dbg_log_send("UPDATE_INV_FULL(equip)", SERVER_UPDATE_INV_FULL, "varshort",
                 payload_len, player->out_cipher.initialized ? 1 : 0);

    network_send(player->socket_fd, out->data, out->position);
    buffer_destroy(out);
}

/*******************************************************************************
 * INTERFACE PACKETS
 ******************************************************************************/

/* 167: IF_SETTAB — exact 225 ordering: short interfaceId, then byte tabIndex */

/*
 * send_sidebar_interface - Assign interface to sidebar tab
 * 
 * @param player        Target player
 * @param tab_slot      Tab slot number (0-13)
 * @param interface_id  Interface ID to display
 * 
 * PACKET STRUCTURE:
 *   Opcode: 167 (IF_SETTAB)
 *   Type:   Fixed (3 bytes payload)
 *   Payload: [interface_id:2][tab_slot:1]
 * 
 * BYTE ORDER (CRITICAL):
 *   Revision 225 uses: [interface_id:2][tab_slot:1]
 *   Some revisions use: [tab_slot:1][interface_id:2]
 *   MUST match client expectations!
 * 
 * TAB SLOTS:
 *   0:  Combat styles    7:  (reserved)
 *   1:  Stats            8:  Friends
 *   2:  Quest journal    9:  Ignore list
 *   3:  Inventory        10: Logout
 *   4:  Equipment        11: Settings
 *   5:  Prayer book      12: Emotes
 *   6:  Magic spellbook  13: Music player
 * 
 * WIRE FORMAT:
 *   ┌──────────┬──────────────────┬──────────┐
 *   │ opcode   │  interface_id    │ tab_slot │
 *   │ (1 byte) │ (2 bytes, BE)    │ (1 byte) │
 *   └──────────┴──────────────────┴──────────┘
 * 
 * EXAMPLE:
 *   send_sidebar_interface(player, 3, 3213);
 *   Sets inventory tab (slot 3) to interface 3213
 * 
 * COMPLEXITY: O(1)
 */
void send_sidebar_interface(Player* player, i32 tab_slot, i32 interface_id) {
    if (!player) return;

    StreamBuffer* out = buffer_create(4);
    buffer_write_header(out, SERVER_IF_SETTAB,
                        player->out_cipher.initialized ? &player->out_cipher : NULL);

    u32 payload_start = buffer_get_position(out);

    buffer_write_short(out, (u16)interface_id, BYTE_ORDER_BIG); /* interface id first */
    buffer_write_byte(out, (u8)tab_slot);                       /* tab index second   */

    int payload_len = (int)(buffer_get_position(out) - payload_start);
    dbg_log_send("IF_SETTAB", SERVER_IF_SETTAB, "fixed",
                 payload_len, player->out_cipher.initialized ? 1 : 0);

    network_send(player->socket_fd, out->data, out->position);
    buffer_destroy(out);
}

/*
 * send_interfaces - Send standard sidebar tab configuration
 * 
 * @param player  Target player
 * 
 * PURPOSE:
 *   Initialize all sidebar tabs to default interfaces
 *   Called once after login or interface reset
 * 
 * STANDARD CONFIGURATION:
 *   Matches official RuneScape revision 225 layout
 * 
 * ALGORITHM:
 *   For each (tab_slot, interface_id) pair:
 *     send_sidebar_interface(player, tab_slot, interface_id)
 * 
 * COMPLEXITY: O(N) where N = 13 tabs
 */
void send_interfaces(Player* player) {
    if (!player) return;

    const struct { u8 tab; u16 iface; } tabs[] = {
        {0, 5855},  /* Combat styles */
        {1, 3917},  /* Stats */
        {2, 638},   /* Quest journal */
        {3, 3213},  /* Inventory */
        {4, 1644},  /* Equipment */
        {5, 5608},  /* Prayer book */
        {6, 1151},  /* Magic spellbook */
        /* 7 reserved in 225 */
        {8, 5065},  /* Friends */
        {9, 5715},  /* Ignore */
        {10, 2449}, /* Logout */
        {11, 904},  /* Settings */
        {12, 147},  /* Emotes */
        {13, 962},  /* Music */
    };
    for (size_t i = 0; i < sizeof(tabs)/sizeof(tabs[0]); ++i) {
        send_sidebar_interface(player, tabs[i].tab, tabs[i].iface);
    }
}

/*
 * send_if_opentop - Set root interface (viewport)
 * 
 * @param player        Target player
 * @param interface_id  Root interface ID
 * 
 * PACKET STRUCTURE:
 *   Opcode: 168 (IF_OPENTOP)
 *   Type:   Fixed (2 bytes payload)
 *   Payload: [interface_id:2]
 * 
 * PURPOSE:
 *   Replace entire screen with new interface
 *   Main game viewport is interface 3559
 * 
 * EXAMPLES:
 *   3559: Main game screen
 *   3559: Login screen
 *   3559: Character design
 * 
 * COMPLEXITY: O(1)
 */
void send_if_opentop(Player* player, i32 interface_id) {
    if (!player) return;
    ISAACCipher* enc = enc_for(player);

    StreamBuffer* out = buffer_create(8);
    buffer_write_header(out, SERVER_IF_OPENTOP, enc);
    u32 payload_start = out->position;

    buffer_write_short(out, (u16)interface_id, BYTE_ORDER_BIG);

    dbg_log_send("IF_OPENTOP", SERVER_IF_OPENTOP, "fixed", (int)(out->position - payload_start), enc != NULL);
    network_send(player->socket_fd, out->data, out->position);
    buffer_destroy(out);
}

/*
 * send_if_settext - Update text label in interface
 * 
 * @param player        Target player
 * @param interface_id  Interface component ID
 * @param text          New text content
 * 
 * PACKET STRUCTURE:
 *   Opcode: 201 (IF_SETTEXT)
 *   Type:   VAR_SHORT
 *   Payload: [interface_id:2][text:string]
 * 
 * USE CASES:
 *   - Update quest progress text
 *   - Change button labels
 *   - Display dynamic values
 * 
 * COMPLEXITY: O(N) where N = text length
 */
void send_if_settext(Player* player, i32 interface_id, const char* text) {
    if (!player || !text) return;
    ISAACCipher* enc = enc_for(player);

    StreamBuffer* out = buffer_create(8u + (u32)strlen(text) + 3u);
    buffer_write_header_var(out, SERVER_IF_SETTEXT, enc, VAR_SHORT);
    u32 payload_start = out->position;

    buffer_write_short(out, (u16)interface_id, BYTE_ORDER_BIG);
    buffer_write_string(out, text);

    buffer_finish_var_header(out, VAR_SHORT);
    dbg_log_send("IF_SETTEXT", SERVER_IF_SETTEXT, "varshort", (int)(out->position - payload_start), enc != NULL);
    network_send(player->socket_fd, out->data, out->position);
    buffer_destroy(out);
}

/*
 * send_if_sethide - Show or hide interface component
 * 
 * @param player        Target player
 * @param interface_id  Interface component ID
 * @param hidden        1 to hide, 0 to show
 * 
 * PACKET STRUCTURE:
 *   Opcode: 26 (IF_SETHIDE)
 *   Type:   Fixed (6 bytes payload)
 *   Payload: [interface_id:2][hidden:4]
 * 
 * HIDDEN VALUE:
 *   0: Component visible
 *   1: Component hidden
 * 
 * COMPLEXITY: O(1)
 */
void send_if_sethide(Player* player, i32 interface_id, i32 hidden) {
    if (!player) return;
    ISAACCipher* enc = enc_for(player);

    StreamBuffer* out = buffer_create(12);
    buffer_write_header(out, SERVER_IF_SETHIDE, enc);
    u32 payload_start = out->position;

    buffer_write_short(out, (u16)interface_id, BYTE_ORDER_BIG);
    buffer_write_int(out, (u32)hidden, BYTE_ORDER_BIG);

    dbg_log_send("IF_SETHIDE", SERVER_IF_SETHIDE, "fixed", (int)(out->position - payload_start), enc != NULL);
    network_send(player->socket_fd, out->data, out->position);
    buffer_destroy(out);
}

/*******************************************************************************
 * VARP (CLIENT VARIABLE) PACKETS
 ******************************************************************************/

/*
 * send_varp_small - Set client variable (1-byte value)
 * 
 * @param player  Target player
 * @param id      Varp ID (0-2000)
 * @param value   New value (0-127)
 * 
 * PACKET STRUCTURE:
 *   Opcode: 150 (VARP_SMALL)
 *   Type:   Fixed (3 bytes payload)
 *   Payload: [id:2][value:1]
 * 
 * USE CASES:
 *   - Boolean settings (0/1)
 *   - Small enums (0-127)
 *   - Percentages (0-100)
 * 
 * EXAMPLES:
 *   send_varp_small(player, 43, 1);   // Set single-button mouse
 *   send_varp_small(player, 173, 100); // Set run energy to 100%
 * 
 * COMPLEXITY: O(1)
 */
void send_varp_small(Player* player, i32 id, i32 value) {
    if (!player) return;
    ISAACCipher* enc = enc_for(player);

    StreamBuffer* out = buffer_create(8);
    buffer_write_header(out, SERVER_VARP_SMALL, enc);
    u32 payload_start = out->position;

    buffer_write_short(out, (u16)id, BYTE_ORDER_BIG);
    buffer_write_byte(out, (u8)value);

    dbg_log_send("VARP_SMALL", SERVER_VARP_SMALL, "fixed", (int)(out->position - payload_start), enc != NULL);
    network_send(player->socket_fd, out->data, out->position);
    buffer_destroy(out);
}

/*
 * send_varp_large - Set client variable (4-byte value)
 * 
 * @param player  Target player
 * @param id      Varp ID (0-2000)
 * @param value   New value (any i32)
 * 
 * PACKET STRUCTURE:
 *   Opcode: 175 (VARP_LARGE)
 *   Type:   Fixed (6 bytes payload)
 *   Payload: [id:2][value:4]
 * 
 * USE CASES:
 *   - Large numbers (>127)
 *   - Timestamps
 *   - Bit flags
 * 
 * COMPLEXITY: O(1)
 */
void send_varp_large(Player* player, i32 id, i32 value) {
    if (!player) return;
    ISAACCipher* enc = enc_for(player);

    StreamBuffer* out = buffer_create(12);
    buffer_write_header(out, SERVER_VARP_LARGE, enc);
    u32 payload_start = out->position;

    buffer_write_short(out, (u16)id, BYTE_ORDER_BIG);
    buffer_write_int(out, (u32)value, BYTE_ORDER_BIG);

    dbg_log_send("VARP_LARGE", SERVER_VARP_LARGE, "fixed", (int)(out->position - payload_start), enc != NULL);
    network_send(player->socket_fd, out->data, out->position);
    buffer_destroy(out);
}

/*******************************************************************************
 * CAMERA PACKETS
 ******************************************************************************/

/*
 * send_cam_reset - Reset camera to default position
 * 
 * @param player  Target player
 * 
 * PACKET STRUCTURE:
 *   Opcode: 239 (CAM_RESET)
 *   Type:   Fixed (0 bytes payload)
 * 
 * PURPOSE:
 *   Return camera to player-controlled mode
 *   Used after cutscenes or fixed-camera events
 * 
 * COMPLEXITY: O(1)
 */
void send_cam_reset(Player* player) {
    if (!player) return;
    ISAACCipher* enc = enc_for(player);

    StreamBuffer* out = buffer_create(2);
    buffer_write_header(out, SERVER_CAM_RESET, enc);
    u32 payload_start = out->position;

    dbg_log_send("CAM_RESET", SERVER_CAM_RESET, "fixed", (int)(out->position - payload_start), enc != NULL);
    network_send(player->socket_fd, out->data, out->position);
    buffer_destroy(out);
}

/*******************************************************************************
 * PLAYER STATE PACKETS
 ******************************************************************************/

/*
 * send_run_energy - Update run energy percentage
 * 
 * @param player  Target player
 * @param energy  Energy percentage (0-100)
 * 
 * PACKET STRUCTURE:
 *   Opcode: 68 (UPDATE_RUNENERGY)
 *   Type:   Fixed (1 byte payload)
 *   Payload: [energy:1]
 * 
 * RUN ENERGY MECHANICS:
 *   - Starts at 100% on login
 *   - Decreases when running
 *   - Regenerates when walking/standing
 *   - Client displays orb in top-right
 * 
 * COMPLEXITY: O(1)
 */
void send_run_energy(Player* player, i32 energy) {
    if (!player) return;
    ISAACCipher* enc = enc_for(player);

    u8 pct = (u8)energy;

    StreamBuffer* out = buffer_create(4);
    buffer_write_header(out, SERVER_UPDATE_RUNENERGY, enc);
    u32 payload_start = out->position;

    buffer_write_byte(out, pct);

    dbg_log_send("UPDATE_RUNENERGY", SERVER_UPDATE_RUNENERGY, "fixed",
                 (int)(out->position - payload_start), enc != NULL);
    network_send(player->socket_fd, out->data, out->position);
    buffer_destroy(out);
}

/*******************************************************************************
 * SESSION CONTROL PACKETS
 ******************************************************************************/

/*
 * send_logout - Force client to logout screen
 * 
 * @param player  Target player
 * 
 * PACKET STRUCTURE:
 *   Opcode: 142 (LOGOUT)
 *   Type:   Fixed (0 bytes payload)
 * 
 * PURPOSE:
 *   Gracefully disconnect player
 *   Client closes connection and shows logout screen
 * 
 * USAGE:
 *   1. Server detects kick condition (timeout, ban, etc.)
 *   2. send_logout(player)
 *   3. Client receives packet
 *   4. Client disconnects socket
 *   5. Server cleans up player slot
 * 
 * ALTERNATIVE:
 *   network_close_socket() - Abrupt disconnect (no packet)
 *   Client shows "Connection lost" instead of normal logout
 * 
 * COMPLEXITY: O(1)
 */
void send_if_close(Player* player) {
    if (!player) return;
    ISAACCipher* enc = enc_for(player);

    StreamBuffer* out = buffer_create(2);
    buffer_write_header(out, SERVER_IF_CLOSE, enc);
    u32 payload_start = out->position;

    dbg_log_send("IF_CLOSE", SERVER_IF_CLOSE, "fixed", (int)(out->position - payload_start), enc != NULL);
    network_send(player->socket_fd, out->data, out->position);
    buffer_destroy(out);
}

void send_logout(Player* player) {
    if (!player) return;
    ISAACCipher* enc = enc_for(player);

    StreamBuffer* out = buffer_create(2);
    buffer_write_header(out, SERVER_LOGOUT, enc);
    u32 payload_start = out->position;

    dbg_log_send("LOGOUT", SERVER_LOGOUT, "fixed", (int)(out->position - payload_start), enc != NULL);
    network_send(player->socket_fd, out->data, out->position);
    buffer_destroy(out);
}
