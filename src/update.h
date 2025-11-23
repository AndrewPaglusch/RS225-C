#ifndef UPDATE_H
#define UPDATE_H

#include "types.h"
#include "player.h"
#include "player_list.h"
#include "buffer.h"

struct GameServer;

void update_players(struct GameServer* server);

/* Minimal per-tick empty player-info (keeps client in sync pre-placement). */
void send_player_info_empty(Player* player);

/* Full "player info" frame used each tick after first-second settling. */
void update_player(Player* player, Player* all_players[], u32 player_count, PlayerTracking* tracking);

#endif /* UPDATE_H */