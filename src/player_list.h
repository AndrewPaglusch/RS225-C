#ifndef PLAYER_LIST_H
#define PLAYER_LIST_H

#include "types.h"
#include "player.h"

/* Player visibility constants */
typedef enum {
    VISIBILITY_DEFAULT = 0, /* Normal visibility */
    VISIBILITY_SOFT = 1,    /* Not implemented */
    VISIBILITY_HARD = 2     /* Hidden from other players */
} PlayerVisibility;

/* Player list structure for efficient multiplayer tracking */
typedef struct {
    Player** players;       /* Array of player pointers indexed by PID */
    u32 capacity;          /* Maximum number of players */
    u32 count;             /* Current number of active players */
    bool* occupied;        /* Bitmap of occupied slots */
    u32 next_pid;          /* Next PID to try for allocation */
} PlayerList;

/* Player tracking info for each player */
typedef struct {
    u16 local_players[MAX_PLAYERS];     /* PIDs of players in local area */
    u32 local_count;                    /* Number of local players */
    bool tracked[MAX_PLAYERS];          /* Which players currently tracking */
    u8 appearance_hashes[MAX_PLAYERS];  /* Appearance version tracking */
} PlayerTracking;

/* Player list functions */
PlayerList* player_list_create(u32 capacity);
void player_list_destroy(PlayerList* list);
bool player_list_add(PlayerList* list, Player* player);
void player_list_remove(PlayerList* list, u16 pid);
Player* player_list_get(PlayerList* list, u16 pid);
u16 player_list_get_next_pid(PlayerList* list);

/* Player visibility functions */
bool player_can_see(const Player* viewer, const Player* target);
bool player_is_within_distance(const Player* p1, const Player* p2);
void player_update_local_players(Player* player, PlayerList* list, PlayerTracking* tracking);

#endif /* PLAYER_LIST_H */