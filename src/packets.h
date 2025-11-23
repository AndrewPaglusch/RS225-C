/*******************************************************************************
 * PACKETS.H - Protocol Opcode Definitions and Packet Length Table
 *******************************************************************************
 * 
 * This header defines all server and client packet opcodes for RuneScape
 * protocol revision 225 (May 2004). Also includes client packet length table
 * for parsing incoming packets.
 * 
 * OPCODE ENCRYPTION:
 *   All opcodes are encrypted with ISAAC cipher after login handshake
 *   Encryption: encrypted_opcode = (opcode + isaac_next()) & 0xFF
 *   Decryption: opcode = (encrypted_opcode - isaac_next()) & 0xFF
 * 
 * PACKET LENGTH TABLE:
 *   PacketLengths[256] maps opcode → payload size
 *   Values:
 *     >= 0: Fixed size (0 means header only)
 *     -1:   Variable byte (next byte is length)
 *     -2:   Variable short (next 2 bytes are length, big-endian)
 * 
 ******************************************************************************/

#ifndef PACKETS_H
#define PACKETS_H

#include "types.h"

/*******************************************************************************
 * SERVER PACKET OPCODES (Server → Client)
 *******************************************************************************
 * 
 * These opcodes are sent by the server to update client state.
 * Values match Jagex's protocol specification.
 * 
 ******************************************************************************/
/* Server packet opcodes (unchanged; matches C++ enum) */
typedef enum {
    SERVER_NPC_INFO = 1,
    SERVER_IF_SETCOLOUR = 2,
    SERVER_CAM_FORCEANGLE = 3,
    SERVER_MESSAGE_GAME = 4,
    SERVER_UPDATE_ZONE_PARTIAL_FOLLOWS = 7,
    SERVER_SYNTH_SOUND = 12,
    SERVER_CAM_SHAKE = 13,
    SERVER_IF_OPENBOTTOM = 14,
    SERVER_UPDATE_INV_CLEAR = 15,
    SERVER_CLEAR_WALKING_QUEUE = 19,
    SERVER_DATA_LOC_DONE = 20,
    SERVER_UPDATE_IGNORELIST = 21,
    SERVER_UPDATE_RUNWEIGHT = 22,
    SERVER_LOC_ADD_CHANGE = 23,
    SERVER_HINT_ARROW = 25,
    SERVER_IF_SETHIDE = 26,
    SERVER_IF_OPENSUB = 28,
    SERVER_CHAT_FILTER_SETTINGS = 32,
    SERVER_MESSAGE_PRIVATE = 41,
    SERVER_LOC_ANIM = 42,
    SERVER_UPDATE_REBOOT_TIMER = 43,
    SERVER_UPDATE_STAT = 44,
    SERVER_IF_SETOBJECT = 46,
    SERVER_OBJ_DEL = 49,
    SERVER_OBJ_ADD = 50,
    SERVER_MIDI_SONG = 54,
    SERVER_LOC_ADD = 59,
    SERVER_UPDATE_RUNENERGY = 68,
    SERVER_MAP_PROJANIM = 69,
    SERVER_SENDMAPREGION = 73,   /* present in some trees; fine to keep */
    SERVER_CAM_MOVETO = 74,
    SERVER_LOC_DEL = 76,
    SERVER_DATA_LAND_DONE = 80,
    SERVER_IF_SETTAB_ACTIVE = 84,
    SERVER_IF_SETMODEL = 87,
    SERVER_UPDATE_INV_FULL = 98,
    SERVER_IF_SETMODEL_COLOUR = 103,
    SERVER_IF_SETTAB_FLASH = 126,
    SERVER_IF_CLOSE = 129,
    SERVER_DATA_LAND = 132,
    SERVER_FINISH_TRACKING = 133,
    SERVER_UPDATE_ZONE_FULL_FOLLOWS = 135,
    SERVER_RESET_ANIMS = 136,
    SERVER_UPDATE_UID192 = 139,
    SERVER_LAST_LOGIN_INFO = 140,
    SERVER_LOGOUT = 142,
    SERVER_IF_SETANIM = 146,
    SERVER_VARP_SMALL = 150,
    SERVER_OBJ_COUNT = 151,
    SERVER_UPDATE_FRIENDLIST = 152,
    SERVER_UPDATE_ZONE_PARTIAL_ENCLOSED = 162,
    SERVER_IF_SETTAB = 167,
    SERVER_IF_OPENTOP = 168,
    SERVER_VARP_LARGE = 175,
    SERVER_PLAYER_INFO = 184,
    SERVER_IF_OPENSTICKY = 185,
    SERVER_SPOTANIM_SPECIFIC = 191,
    SERVER_RESET_CLIENT_VARCACHE = 193,
    SERVER_IF_OPENSIDEBAR = 195,
    SERVER_IF_SETPLAYERHEAD = 197,
    SERVER_IF_SETTEXT = 201,
    SERVER_IF_SETNPCHEAD = 204,
    SERVER_IF_SETPOSITION = 209,
    SERVER_MIDI_JINGLE = 212,
    SERVER_UPDATE_INV_PARTIAL = 213,
    SERVER_DATA_LOC = 220,
    SERVER_OBJ_REVEAL = 223,
    SERVER_ENABLE_TRACKING = 226,
    SERVER_LOAD_AREA = 237,
    SERVER_CAM_RESET = 239,
    SERVER_IF_IAMOUNT = 243,
    SERVER_IF_MULTIZONE = 254
} ServerPacket;

/*******************************************************************************
 * CLIENT PACKET LENGTH TABLE (Client → Server)
 *******************************************************************************
 * 
 * Maps client packet opcode to expected payload size.
 * Used during packet parsing to determine how many bytes to read.
 * 
 * ENCODING:
 *   -2: Variable short (read 2-byte big-endian length, then payload)
 *   -1: Variable byte (read 1-byte length, then payload)
 *    0: Header only (no payload)
 *   >0: Fixed size payload in bytes
 * 
 * PARSING ALGORITHM:
 *   1. Read encrypted opcode byte
 *   2. Decrypt: opcode = (encrypted - isaac_next()) & 0xFF
 *   3. len = PacketLengths[opcode]
 *   4. If len == -1: read 1 byte for length
 *   5. If len == -2: read 2 bytes (big-endian) for length
 *   6. Read 'len' bytes of payload
 * 
 * VALUES:
 *   Protocol revision 225 client packet sizes
 * 
 ******************************************************************************/
/*
 * Protocol 225 client packet lengths (client -> server).
 * -1 = variable byte, -2 = variable short, 0+ = fixed size
 * COPIED to match the working C++ server's Packet.h table.
 */
static const i8 PacketLengths[256] = {
    0, 0, 2, 0, -1, 0, 6, 4, 2, 8, // 0-9
    0, 8, 0, 0, 0, 0, 0, 4, 0, 0,  // 10-19
    0, 0, 0, 0, 0, 0, 0, 2, 0, 0,  // 20-29
    3, 6, 0, 0, 0, 0, 0, 0, 6, 0,  // 30-39
    6, 0, 0, 0, 0, 0, 0, 0, 8, 0,  // 40-49
    0, 0, 13, 2, 0, 0, 0, 0, 0, 6, // 50-59
    0, 0, 0, 0, 0, 0, 4, 0, 0, 0,  // 60-69
    0, 6, 0, 0, 0, 12, 0, 0, 0, 8, // 70-79
    0, -2, 0, 0, 0, 0, 0, 0, 4, 0, // 80-89
    0, 0, 0, -1, 0, 0, 6, 6, -1, 0,// 90-99
    2, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 100-109
    0, 0, 0, 2, 0, 0, 6, 0, 8, 0,  // 110-119
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 120-129
    12, 0, 0, 6, 4, 0, 0, 0, 8, 0, // 130-139
    6, 0, 0, 0, 0, 0, -1, 0, -1, 0,// 140-149
    -1, 0, 0, 0, 0, 2, 0, 6, -1, 6,// 150-159
    0, 0, 0, 0, 2, -1, 0, 0, 0, 0, // 160-169
    0, 8, 6, 0, 0, 1, 2, 4, 6, 0,  // 170-179
    0, -1, 0, 0, 0, 2, 0, 0, 0, 6, // 180-189
    10, 0, 0, 0, 2, 0, 6, 0, 0, 0, // 190-199 (190=10, 194=2, 195=6)
    6, 0, 8, 0, 0, 0, 2, 0, 0, 0,  // 200-209 (200=6, 202=8, 206=2)
    0, 6, 6, 0, 0, 3, 0, 0, 0, -1, // 210-219 (211=6, 212=6, 215=3, 219=-1)
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 220-229 (no changes)
    0, 0, 0, 1, 0, 2, 4, 4, 1, 12, // 230-239 (233=1, 235=2, 236=4, 237=4, 238=1, 239=12)
    0, 0, 0, 0, 3, 6, 0, 6, 8, 0,  // 240-249 (244=3, 245=6, 247=6, 248=8)
    0, 0, 0, 0, 0, 0               // 250-255
};

#endif /* PACKETS_H */
