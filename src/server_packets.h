/*******************************************************************************
 * SERVER_PACKETS.H - Server-to-Client Packet Interface
 *******************************************************************************
 * 
 * This header declares all outgoing packet construction functions for the
 * RuneScape protocol revision 225. Each function encapsulates packet framing,
 * serialization, encryption, and transmission.
 * 
 * PACKET CONSTRUCTION PATTERN:
 *   All functions follow the pattern:
 *   1. Validate player pointer
 *   2. Create StreamBuffer with size estimate
 *   3. Write encrypted opcode + variable length header (if applicable)
 *   4. Serialize payload data
 *   5. Finalize variable length header (backfill length field)
 *   6. Transmit via network_send()
 *   7. Destroy buffer
 * 
 * OPCODE ENCRYPTION:
 *   All opcodes encrypted with player->out_cipher (ISAAC cipher)
 *   Encryption occurs in buffer_write_header() and buffer_write_header_var()
 * 
 * FRAME TYPES:
 *   - Fixed:     Opcode + fixed-size payload
 *   - VAR_BYTE:  Opcode + 1-byte length + payload (max 255 bytes)
 *   - VAR_SHORT: Opcode + 2-byte length + payload (max 65535 bytes)
 * 
 ******************************************************************************/

#ifndef SERVER_PACKETS_H
#define SERVER_PACKETS_H

#include "types.h"
#include "player.h"
#include "packets.h"

/* Packet sending functions */

/*******************************************************************************
 * OUTGOING PACKET FUNCTIONS
 ******************************************************************************/

/*
 * send_player_message - Send game message to client
 * 
 * @param player   Target player connection
 * @param message  Null-terminated string (newline appended automatically)
 * 
 * Opcode: SERVER_MESSAGE_GAME (4)
 * Frame:  VAR_BYTE
 * Payload: String with newline terminator
 */
void send_player_message(Player* player, const char* message);

/*
 * send_sidebar_interface - Assign interface to sidebar tab slot
 * 
 * @param player        Target player
 * @param tab_slot      Tab index (0-13)
 * @param interface_id  Interface to display in tab
 * 
 * Opcode: SERVER_IF_SETTAB (167)
 * Frame:  Fixed (3 bytes)
 * Payload: [tab_slot:1][interface_id:2 big-endian]
 */
void send_sidebar_interface(Player* player, i32 tab_slot, i32 interface_id);

/*
 * send_player_stats - Send all skill levels and XP
 * 
 * @param player  Target player
 * 
 * Sends 23 UPDATE_STAT packets (one per skill)
 * Each packet: Opcode 44, Fixed frame, 6 bytes payload
 * Payload: [skill_id:1][level:1][xp:4 big-endian]
 */
void send_player_stats(Player* player);

/*
 * send_inventory - Send inventory contents
 * 
 * @param player  Target player
 * 
 * Opcode: SERVER_UPDATE_INV_FULL (98)
 * Frame:  VAR_SHORT
 * Payload: [interface_id:2][item_count:1][items...]
 * Current implementation sends empty inventory
 */
void send_inventory(Player* player);

/*
 * send_equipment - Send equipment interface state
 * 
 * @param player  Target player
 * 
 * Opcode: SERVER_UPDATE_INV_FULL (98)
 * Frame:  VAR_SHORT
 * Sends equipment interface (1688) contents
 */
void send_equipment(Player* player);

/*
 * send_if_opentop - Set root interface (main viewport)
 * 
 * @param player        Target player
 * @param interface_id  Interface to set as root
 * 
 * Opcode: SERVER_IF_OPENTOP (168)
 * Frame:  Fixed (2 bytes)
 * Payload: [interface_id:2 big-endian]
 */
void send_if_opentop(Player* player, i32 interface_id);

/*
 * send_cam_reset - Reset camera to default position
 * 
 * @param player  Target player
 * 
 * Opcode: SERVER_CAM_RESET (239)
 * Frame:  Fixed (0 bytes payload)
 */
void send_cam_reset(Player* player);

/*
 * send_run_energy - Update run energy value
 * 
 * @param player  Target player
 * @param energy  Energy level (0-100)
 * 
 * Opcode: SERVER_UPDATE_RUNENERGY (68)
 * Frame:  Fixed (1 byte)
 * Payload: [energy:1]
 */
void send_run_energy(Player* player, i32 energy);

/*
 * send_varp_small - Set client variable (1-byte value)
 * 
 * @param player  Target player
 * @param id      VARP index
 * @param value   Value (0-127)
 * 
 * Opcode: SERVER_VARP_SMALL (150)
 * Frame:  Fixed (2 bytes)
 * Payload: [id:1][value:1 signed]
 */
void send_varp_small(Player* player, i32 id, i32 value);

/*
 * send_varp_large - Set client variable (4-byte value)
 * 
 * @param player  Target player
 * @param id      VARP index
 * @param value   Value (any i32)
 * 
 * Opcode: SERVER_VARP_LARGE (175)
 * Frame:  Fixed (6 bytes)
 * Payload: [id:2 big-endian][value:4 big-endian]
 */
void send_varp_large(Player* player, i32 id, i32 value);

/*
 * send_if_settext - Update interface text label
 * 
 * @param player        Target player
 * @param interface_id  Interface component to update
 * @param text          New text content
 * 
 * Opcode: SERVER_IF_SETTEXT (201)
 * Frame:  VAR_SHORT
 * Payload: [interface_id:2 big-endian][text with newline]
 */
void send_if_settext(Player* player, i32 interface_id, const char* text);

/*
 * send_if_sethide - Show or hide interface component
 * 
 * @param player        Target player
 * @param interface_id  Interface component
 * @param hidden        1 = hide, 0 = show
 * 
 * Opcode: SERVER_IF_SETHIDE (26)
 * Frame:  Fixed (3 bytes)
 * Payload: [interface_id:2 big-endian][hidden:1]
 */
void send_if_sethide(Player* player, i32 interface_id, i32 hidden);

/*
 * send_if_close - Close current interface
 * 
 * @param player  Target player
 * 
 * Opcode: SERVER_IF_CLOSE (129)
 * Frame:  Fixed (0 bytes payload)
 * Closes the currently open modal/sticky interface
 */
void send_if_close(Player* player);

/*
 * send_logout - Initiate client logout sequence
 * 
 * @param player  Target player
 * 
 * Opcode: SERVER_LOGOUT (142)
 * Frame:  Fixed (0 bytes payload)
 * Causes client to return to login screen
 */
void send_logout(Player* player);

/*
 * send_interfaces - Initialize all sidebar tab interfaces
 * 
 * @param player  Target player
 * 
 * Sends IF_SETTAB packets for all 13 sidebar tabs
 * Called once during login to setup UI
 */
void send_interfaces(Player* player);

#endif /* SERVER_PACKETS_H */