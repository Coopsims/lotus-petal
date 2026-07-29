#include "game.h"

#include "persist.h"
#include "net_link.h"
#include "screens/screen_life.h"
#include "screens/screen_commander.h"
#include "screens/screen_counters.h"

static int s_turn = 1;
static int s_players = 4;   /* default pod size; the boot screen can change it */
static game_mode_t s_mode = GAME_MODE_LOCAL;
static bool s_eliminated;

typedef struct { int seat; int roll; int order; } rank_t;

game_mode_t game_mode(void)
{
    return s_mode;
}

void game_set_mode(game_mode_t m)
{
    s_mode = m;
}

static void resolve_first_player(void);
static void resolve_seating(void);
static void clear_for_new_round(void);

void game_sync_remote(void)
{
    if (s_mode != GAME_MODE_REMOTE) return;

    /* The table can be larger than the number of dials — not everyone playing
     * owns one — so the pod size is its own shared setting, not a head count. */
    int n = net_link_players();
    if (n < net_link_member_count()) n = net_link_member_count();
    if (n < 2) n = 2;
    if (n != s_players) game_set_player_count(n);

    /* Another dial started a new round — follow it. */
    if (net_link_take_new_round()) clear_for_new_round();

    net_link_publish(screen_life_get_life(), !s_eliminated);
    resolve_first_player();
    resolve_seating();
}

bool game_is_eliminated(void) { return s_eliminated; }

void game_set_eliminated(bool out)
{
    s_eliminated = out;
    persist_mark_dirty();
}

/* Lethal thresholds: no life left, 21 from a single commander, or ten poison.
 * Reaching one only offers the choice — it never takes it. */
bool game_can_eliminate(void)
{
    return screen_life_get_life() <= 0 ||
           screen_commander_max_damage() >= 21 ||
           screen_counters_poison() >= 10;
}

static bool remote_live(void)
{
    return s_mode == GAME_MODE_REMOTE && net_link_active();
}

static int build_ranks(rank_t *out, int max);

int game_total_players(void)
{
    return remote_live() ? net_link_players() : s_players;
}

void game_set_total_players(int n)
{
    if (!remote_live()) return;
    if (n < 2) n = 2;
    if (n > 8) n = 8;
    if (n < net_link_member_count()) n = net_link_member_count();
    net_link_set_round(net_link_active_pos(), n, net_link_turn());
}

bool game_has_first_player(void)
{
    return remote_live() && net_link_round_valid();
}

bool game_is_my_turn(void)
{
    return game_has_first_player() && net_link_my_order() == net_link_active_pos();
}

/* A position nobody has claimed belongs to a player without a dial. */
bool game_active_is_unmanned(void)
{
    if (!game_has_first_player()) return false;
    int pos = net_link_active_pos();
    rank_t r[NET_MAX_MEMBERS];
    int n = build_ranks(r, NET_MAX_MEMBERS);
    for (int i = 0; i < n; i++) {
        if (r[i].order == pos) return false;
    }
    return true;
}

/* One entry per dial: where it sits (MAC-derived seat), its d20, and the turn
 * position it claimed. */
static int build_ranks(rank_t *out, int max)
{
    int n = 0;
    if (max <= 0) return 0;

    out[n].seat  = net_link_self_seat();
    out[n].roll  = net_link_my_roll();
    out[n].order = net_link_my_order();
    n++;

    net_member_t others[NET_MAX_OTHERS];
    int m = net_link_others(others, NET_MAX_OTHERS);
    for (int i = 0; i < m && n < max; i++) {
        out[n].seat  = others[i].seat;
        out[n].roll  = others[i].roll;
        out[n].order = others[i].order;
        n++;
    }
    return n;
}

/* Highest roll first — used only to decide who starts. */
static int build_order_by_roll(rank_t *out, int max)
{
    int n = build_ranks(out, max);
    for (int i = 1; i < n; i++) {                 /* insertion sort, n <= 8 */
        rank_t key = out[i];
        int j = i - 1;
        while (j >= 0 && (out[j].roll < key.roll ||
                          (out[j].roll == key.roll && out[j].seat > key.seat))) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }
    return n;
}

void game_roll_for_first(int d20)
{
    if (!remote_live()) return;
    net_link_set_roll(d20);
}

first_status_t game_first_status(void)
{
    if (!remote_live()) return FIRST_NOT_REMOTE;

    if (!net_link_round_valid()) {
        if (net_link_my_roll() <= 0) return FIRST_NEED_ROLL;
        rank_t r[NET_MAX_MEMBERS];
        int n = build_order_by_roll(r, NET_MAX_MEMBERS);
        for (int i = 0; i < n; i++) {
            if (r[i].roll <= 0) return FIRST_WAITING;   /* someone still to roll */
        }
        if (n >= 2 && r[0].roll == r[1].roll) return FIRST_TIE;
        return FIRST_WAITING;   /* settled — the winner is about to announce it */
    }

    /* First player is known; everyone else picks where they sit. */
    if (net_link_my_order() <= 0) return FIRST_PICK_SEAT;

    rank_t r[NET_MAX_MEMBERS];
    int n = build_ranks(r, NET_MAX_MEMBERS);
    for (int i = 0; i < n; i++) {
        if (r[i].order <= 0) return FIRST_WAIT_SEATS;
    }
    return FIRST_DONE;
}

int game_seat_map(unsigned char *taken, int max)
{
    rank_t r[NET_MAX_MEMBERS];
    int n = build_ranks(r, NET_MAX_MEMBERS);
    int players = net_link_players();
    for (int i = 0; i < max; i++) taken[i] = 0;
    for (int i = 0; i < n; i++) {
        int o = r[i].order;
        /* Our own claim is not "taken" from our point of view — we may re-pick. */
        if (o > 0 && o < max && r[i].seat != net_link_self_seat()) taken[o] = 1;
    }
    return players;
}

void game_claim_seat(int pos)
{
    if (!remote_live()) return;
    net_link_set_order(pos);
    persist_mark_dirty();
}

/* Two dials need no seating discussion: whoever did not win is second. Also
 * settles a clash, where two dials claimed the same position — the lower seat
 * keeps it and the other picks again. Both rules are computed identically
 * everywhere, so nobody has to agree with anybody. */
static void resolve_seating(void)
{
    if (!remote_live() || !net_link_round_valid()) return;

    rank_t r[NET_MAX_MEMBERS];
    int n = build_ranks(r, NET_MAX_MEMBERS);
    int self = net_link_self_seat();
    int mine = net_link_my_order();

    if (mine <= 0) {
        if (n == 2 && net_link_players() == 2 && net_link_my_roll() > 0 &&
            net_link_active_pos() > 0) net_link_set_order(2);   /* nothing to choose */
        return;
    }
    for (int i = 0; i < n; i++) {
        if (r[i].seat == self) continue;
        if (r[i].order == mine && r[i].seat < self) {
            net_link_set_order(0);   /* they were here first; pick again */
            return;
        }
    }
}

/* Once every dial has rolled, the highest roller announces the result. Only the
 * winner writes it, so the epoch never races. A tie at the top clears everyone's
 * roll and the table rolls again. */
static void resolve_first_player(void)
{
    if (!remote_live() || net_link_round_valid()) return;

    rank_t r[NET_MAX_MEMBERS];
    int n = build_order_by_roll(r, NET_MAX_MEMBERS);
    if (n < 2) return;                      /* nobody to play against yet */
    for (int i = 0; i < n; i++) {
        if (r[i].roll <= 0) return;         /* still waiting on a roll */
    }

    if (r[0].roll == r[1].roll) {
        net_link_set_roll(0);               /* tie: every dial re-rolls */
        return;
    }
    if (r[0].seat == net_link_self_seat()) {
        s_turn = 1;
        /* The roll-off only sees dials. If someone at the table is playing
         * without one they may well have rolled higher on real dice, and we
         * would have no way of knowing — so in that case winning here does not
         * seat you first. Everyone picks a position instead, and first place is
         * simply left empty if it belongs to a player with no dial. */
        if (net_link_players() <= net_link_member_count()) {
            net_link_set_order(1);
        }
        net_link_set_round(1, net_link_players(), 1);
        persist_mark_dirty();
    }
}

game_result_t game_result(void)
{
    if (!remote_live() || !net_link_round_valid()) return GAME_RESULT_NONE;

    /* Only meaningful when every seat has a dial: a player without one has no
     * way to tell us they are out, so we would be guessing. */
    int players = net_link_players();
    if (players != net_link_member_count()) return GAME_RESULT_NONE;
    if (players < 2) return GAME_RESULT_NONE;

    if (s_eliminated) return GAME_RESULT_LOSE;

    net_member_t others[NET_MAX_OTHERS];
    int n = net_link_others(others, NET_MAX_OTHERS);
    if (n < 1) return GAME_RESULT_NONE;
    for (int i = 0; i < n; i++) {
        if (others[i].alive) return GAME_RESULT_NONE;
    }
    return GAME_RESULT_WIN;      /* everyone else has bowed out */
}

/* Clear our own game. The link, the PIN, the table size AND the seating all
 * survive — a new round is a fresh game among the same players in the same
 * order, not a fresh table. Rolls and claimed positions are left alone. */
static void clear_for_new_round(void)
{
    s_eliminated = false;
    s_turn = 1;
    game_reset();
}

void game_begin_new_round(void)
{
    if (remote_live()) net_link_begin_new_round();
    clear_for_new_round();
}

void game_pass_turn(void)
{
    if (!remote_live()) {
        game_next_turn();   /* one dial: passing is simply the next turn */
        return;
    }
    /* Pass your own turn — or one belonging to a player with no dial, since
     * otherwise nobody at the table could move it on. */
    if (!game_is_my_turn() && !game_active_is_unmanned()) return;

    int players = net_link_players();
    if (players < 2) players = 2;
    uint32_t turn = net_link_turn();

    /* Step past anyone who has bowed out. Only dials can report being out, so a
     * seat with no dial is always played. Bounded by the table size in case
     * everybody is somehow out. */
    net_member_t others[NET_MAX_OTHERS];
    int m = net_link_others(others, NET_MAX_OTHERS);
    int next = net_link_active_pos();
    bool lap = false;
    for (int step = 0; step < players; step++) {
        next = next % players + 1;
        if (next == 1) lap = true;

        bool out = false;
        for (int i = 0; i < m; i++) {
            if (others[i].order == next && !others[i].alive) { out = true; break; }
        }
        if (next == net_link_my_order() && s_eliminated) out = true;
        if (!out) break;
    }
    if (lap) turn++;

    net_link_set_round(next, players, turn);
    s_turn = (int)turn;
    if (lap) screen_counters_new_turn();  /* zero Storm et al. on the new turn */
    persist_mark_dirty();
}

int game_turn(void)
{
    /* In a linked game the turn belongs to the table, not to this dial. */
    if (s_mode == GAME_MODE_REMOTE && net_link_active() && net_link_round_valid()) {
        return (int)net_link_turn();
    }
    return s_turn;
}

int game_player_count(void)
{
    return s_players;
}

void game_set_player_count(int n)
{
    if (n < 2) n = 2;
    if (n > 8) n = 8;
    s_players = n;
    /* One wedge per seat, ours included — a commander can damage its own
     * controller, and that has to be recordable somewhere. */
    screen_commander_set_opponents(n);
    persist_mark_dirty();
}

void game_set_turn(int n)
{
    if (n < 1) n = 1;
    s_turn = n;
}

void game_next_turn(void)
{
    s_turn++;
    screen_counters_new_turn(); /* zero Storm et al. */
    persist_mark_dirty();
}

void game_undo_turn(void)
{
    if (s_turn > 1) {
        s_turn--;
    }
    persist_mark_dirty();
}

void game_reset(void)
{
    s_turn = 1;
    s_eliminated = false;
    screen_life_reset();
    screen_commander_reset();
    screen_counters_reset();
    persist_mark_dirty();
}
