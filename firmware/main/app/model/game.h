/*
 * game.h — shared game state (turn counter) and the new-game reset.
 *
 * Turn control is reachable from two places (the Life screen's Next-Turn button
 * and the Tools overlay's Undo), so it lives here rather than in one screen.
 */
#ifndef LOTUS_GAME_H
#define LOTUS_GAME_H

#include <stdbool.h>

/** Starting life total. Commander is the format this is built around, so 40 —
 *  but it is referenced rather than repeated, including by the boot logic that
 *  decides whether a saved game was ever actually played. */
#define GAME_STARTING_LIFE 40

/** How the table is being tracked: everything on this dial, or one seat of a
 *  group of dials linked over ESP-NOW (see net/net_link.h). */
typedef enum {
    GAME_MODE_LOCAL = 0,
    GAME_MODE_REMOTE,
} game_mode_t;

game_mode_t game_mode(void);
void game_set_mode(game_mode_t m);

/** In Remote the pod size is not chosen — it follows however many dials are
 *  currently linked. Call from the network tick; resizes the game when the
 *  member count changes. */
void game_sync_remote(void);

/** Current turn number (starts at 1). */
int game_turn(void);

/** Number of players in the game, including this device's owner (2..8). */
int game_player_count(void);

/** Set the player count (clamped to 2..8). Reconfigures the Commander-damage
 *  screen to one pie slice per opponent (players - 1). */
void game_set_player_count(int n);

/** Set the turn counter to `n` (min 1). Used when restoring a saved game. */
void game_set_turn(int n);

/** Advance one turn: bumps the counter and zeroes per-turn counters (Storm). */
void game_next_turn(void);

/** Pass the turn on. Locally that is just the next turn; in Remote it hands the
 *  turn to the next seat, and the turn number only advances once it comes back
 *  around to the player who starts the round. Does nothing in Remote when it is
 *  not this dial's turn — you can only pass what you hold. */
void game_pass_turn(void);

/** Total players at the table, including any without a dial. */
int game_total_players(void);

/** Set the table size (2..8). Shared with the other dials. Use when some players
 *  at the table have no dial of their own. */
void game_set_total_players(int n);

/** This dial's turn position, 1-based. Always 1 in a local game, where there is
 *  only one seat that matters. In a linked game it is the position claimed on the
 *  seat picker, and 0 until seats are settled. */
int game_my_position(void);

/* ---- things exactly one player at the table holds ----
 * The monarchy and the initiative belong to a single player by the rules, so they
 * are stored as WHICH turn position holds them (0 = nobody) rather than as a flag
 * on each dial. A per-dial flag lets the whole table claim the crown at once with
 * no way to settle it, which is worse than not tracking it.
 *
 * In a linked game these are shared round state and every dial agrees. In a local
 * game there is no table to share with, so the counters on the Counters screen
 * remain the plain toggles they always were and these are not consulted. */

/** Turn position holding it, or 0 for nobody. Meaningful in a linked game. */
int  game_monarch(void);
int  game_initiative(void);

/** Claim for `pos`, or pass 0 to leave it unheld. Ignored in a local game. */
void game_set_monarch(int pos);
void game_set_initiative(int pos);

/** Out of the game. Never set automatically: life can legitimately dip below
 *  zero mid-resolution, so the player decides when they are actually dead. */
bool game_is_eliminated(void);
void game_set_eliminated(bool out);

/** True when this player has hit a lethal threshold and could bow out. */
bool game_can_eliminate(void);

/** How the round ended, for the result screen. Only decided in a game where
 *  every player has a dial — with someone playing without one we cannot know
 *  whether they are still alive. */
typedef enum {
    GAME_RESULT_NONE = 0,
    GAME_RESULT_WIN,
    GAME_RESULT_LOSE,
} game_result_t;

game_result_t game_result(void);

/** Clear the game for another round, keeping the dials linked and seated at the
 *  same table. Tells the other dials to do the same. */
void game_begin_new_round(void);

/** True when the position being played belongs to a player with no dial, so
 *  nobody's Pass button would otherwise work. Any dial may pass for them. */
bool game_active_is_unmanned(void);

/** True when this dial holds the turn. Always false in Local, and false in
 *  Remote until a first player has been rolled. */
bool game_is_my_turn(void);

/** True once a first player has been chosen for this game. */
bool game_has_first_player(void);

/** Where the opening d20 roll-off has got to. */
typedef enum {
    FIRST_NOT_REMOTE = 0, /* local game — no roll-off */
    FIRST_NEED_ROLL,      /* this dial still has to roll */
    FIRST_WAITING,        /* rolled; waiting on the other dials */
    FIRST_TIE,            /* tied at the top — everyone rolls again */
    FIRST_PICK_SEAT,      /* first player known; choose where you sit */
    FIRST_WAIT_SEATS,     /* seated; waiting on the others to choose */
    FIRST_DONE,           /* order settled */
} first_status_t;

/** Turn positions still unclaimed, for the seat picker. Writes `1` into
 *  `taken[k]` when position k (1-based) is already claimed by someone.
 *  Returns the number of players. */
int game_seat_map(unsigned char *taken, int max);

/** Claim turn position `pos` (2..players) for this dial. */
void game_claim_seat(int pos);

first_status_t game_first_status(void);

/** Record this dial's d20 for the roll-off. Highest roll goes first and the rest
 *  follow in descending order, so one roll each settles the whole turn order. */
void game_roll_for_first(int d20);

/** Step the turn counter back one (never below 1). */
void game_undo_turn(void);

/** New game: turn back to 1 and reset life + every counter/token. */
void game_reset(void);

#endif /* LOTUS_GAME_H */
