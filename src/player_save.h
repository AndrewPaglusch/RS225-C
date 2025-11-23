/*******************************************************************************
 * PLAYER_SAVE.H - Player Data Persistence System
 *******************************************************************************
 * 
 * Implements binary save/load format compatible with TypeScript server.
 * 
 * SAVE FILE FORMAT (Version 6):
 * 
 *   Header (4 bytes):
 *     uint16_t magic    = 0x2004
 *     uint16_t version  = 6
 * 
 *   Position (5 bytes):
 *     uint16_t x
 *     uint16_t z
 *     uint8_t  level
 * 
 *   Appearance (12 bytes):
 *     uint8_t  body[7]    (body parts: -1 stored as 255)
 *     uint8_t  colors[5]  (color indices)
 * 
 *   Player Data (7 bytes):
 *     uint8_t  gender     (0=male, 1=female)
 *     uint16_t runenergy  (0-10000)
 *     uint32_t playtime   (ticks logged in)
 * 
 *   Stats (21 skills × 5 bytes = 105 bytes):
 *     For each of 21 skills:
 *       uint32_t experience
 *       uint8_t  level  (current level, can be boosted/drained)
 * 
 *   Footer (4 bytes):
 *     uint32_t crc32  (checksum of all previous bytes)
 * 
 * TOTAL MINIMUM SIZE: 137 bytes (without inventories/varps)
 * 
 * SAVE FILE LOCATION:
 *   data/players/default/{username}.sav
 * 
 ******************************************************************************/

#ifndef PLAYER_SAVE_H
#define PLAYER_SAVE_H

#include "types.h"
#include "player.h"

/* Save file constants */
#define PLAYER_SAVE_MAGIC   0x2004    /* Magic number identifier */
#define PLAYER_SAVE_VERSION 6         /* Current save format version */
#define PLAYER_SAVE_DIR     "data/players/default"

/* Skill constants */
#define SKILL_COUNT 21

/* Skill indices (matching RuneScape skill order) */
typedef enum {
    SKILL_ATTACK = 0,
    SKILL_DEFENCE,
    SKILL_STRENGTH,
    SKILL_HITPOINTS,
    SKILL_RANGED,
    SKILL_PRAYER,
    SKILL_MAGIC,
    SKILL_COOKING,
    SKILL_WOODCUTTING,
    SKILL_FLETCHING,
    SKILL_FISHING,
    SKILL_FIREMAKING,
    SKILL_CRAFTING,
    SKILL_SMITHING,
    SKILL_MINING,
    SKILL_HERBLORE,
    SKILL_AGILITY,
    SKILL_THIEVING,
    SKILL_SLAYER,
    SKILL_FARMING,
    SKILL_RUNECRAFT
} SkillId;

/* Player appearance data */
typedef struct {
    i8 body[7];      /* Body parts (-1 = hidden) */
    u8 colors[5];    /* Color indices */
    u8 gender;       /* 0=male, 1=female */
} PlayerAppearance;

/* Player skill data */
typedef struct {
    u32 experience;  /* XP for this skill */
    u8 level;        /* Current level (base level, can be temporarily boosted) */
} PlayerSkill;

/* Extended player data for persistence */
typedef struct {
    /* Appearance */
    PlayerAppearance appearance;
    
    /* Stats */
    PlayerSkill skills[SKILL_COUNT];
    
    /* Energy & Playtime */
    u16 runenergy;   /* Run energy (0-10000) */
    u32 playtime;    /* Total ticks logged in */
    
    /* Last login timestamp */
    u64 last_login;  /* Milliseconds since epoch */
} PlayerData;

/* Function prototypes */

/*
 * player_save - Save player data to disk
 * 
 * @param player  Player to save
 * @return        true on success, false on error
 * 
 * Creates/overwrites save file at: data/players/default/{username}.sav
 * Uses atomic save (writes to .tmp file, then renames)
 */
bool player_save(const Player* player);

/*
 * player_load - Load player data from disk
 * 
 * @param player  Player to load data into
 * @return        true if save file exists and was loaded, false if new player
 * 
 * Reads save file from: data/players/default/{username}.sav
 * If file doesn't exist or is invalid, initializes with default values
 */
bool player_load(Player* player);

/*
 * player_data_init - Initialize player data with defaults
 * 
 * @param player  Player to initialize
 * 
 * Sets default values for new players:
 * - All skills at level 1 (except Hitpoints at 10)
 * - Default appearance (male, basic clothing)
 * - Full run energy
 * - Zero playtime
 */
void player_data_init(Player* player);

/*
 * player_get_save_path - Get save file path for username
 * 
 * @param username  Player username
 * @param buffer    Output buffer for path
 * @param buf_size  Size of buffer
 * @return          Number of characters written (excluding null terminator)
 * 
 * Format: data/players/default/{username}.sav
 */
int player_get_save_path(const char* username, char* buffer, size_t buf_size);

#endif /* PLAYER_SAVE_H */
