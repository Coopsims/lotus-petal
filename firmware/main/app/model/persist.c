#include "persist.h"

#include "game.h"
#include "screens/screen_life.h"
#include "screens/screen_commander.h"
#include "screens/screen_counters.h"

#include "petal_hal.h"
#include <string.h>

#define PERSIST_NS   "game"
#define PERSIST_KEY  "snap"
#define PERSIST_MAGIC   0x4C4F5431u   /* 'LOT1' */
#define PERSIST_VERSION 2

#define MAX_OPP 8   /* one per seat, including our own */
#define MAX_CNT 24

/* Fixed-layout blob so a struct change is caught by the version check. */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t  players;
    uint8_t  opp_count;
    uint8_t  counter_n;
    uint8_t  _pad[3];
    int32_t  turn;
    int32_t  life;
    int16_t  opp_dmg[MAX_OPP];
    int16_t  counters[MAX_CNT];
} snap_t;

static snap_t s_loaded;
static bool   s_have;
static bool   s_dirty;

void persist_init(void)
{
    /* Storage rejects a blob of a different size for us, and the magic/version
     * pair catches a same-size layout change, so a snapshot from an incompatible
     * build is ignored rather than misread. */
    s_have = petal_kv_get(PERSIST_NS, PERSIST_KEY, &s_loaded, sizeof(s_loaded)) &&
             s_loaded.magic == PERSIST_MAGIC &&
             s_loaded.version == PERSIST_VERSION;
}

bool persist_has_saved_game(void)
{
    return s_have;
}

int persist_saved_turn(void)    { return s_have ? s_loaded.turn : 0; }
int persist_saved_life(void)    { return s_have ? s_loaded.life : 0; }
int persist_saved_players(void) { return s_have ? s_loaded.players : 0; }

void persist_resume(void)
{
    if (!s_have) return;

    /* Player count first: it rebuilds the commander grid (and zeros it), so the
     * damage values must be applied afterwards. */
    game_set_player_count(s_loaded.players);
    game_set_turn(s_loaded.turn);
    screen_life_set_life(s_loaded.life);

    int oc = screen_commander_opponent_count();
    for (int i = 0; i < oc && i < MAX_OPP; i++) {
        screen_commander_set_damage(i, s_loaded.opp_dmg[i]);
    }

    int cn = screen_counters_num();
    for (int i = 0; i < cn && i < s_loaded.counter_n && i < MAX_CNT; i++) {
        screen_counters_set_value(i, s_loaded.counters[i]);
    }
}

void persist_mark_dirty(void)
{
    s_dirty = true;
}

void persist_flush(void)
{
    if (!s_dirty) return;

    snap_t s;
    memset(&s, 0, sizeof(s));
    s.magic   = PERSIST_MAGIC;
    s.version = PERSIST_VERSION;
    s.players = (uint8_t)game_player_count();
    s.turn    = game_turn();
    s.life    = screen_life_get_life();

    s.opp_count = (uint8_t)screen_commander_opponent_count();
    for (int i = 0; i < s.opp_count && i < MAX_OPP; i++) {
        s.opp_dmg[i] = (int16_t)screen_commander_get_damage(i);
    }

    int cn = screen_counters_num();
    if (cn > MAX_CNT) cn = MAX_CNT;
    s.counter_n = (uint8_t)cn;
    for (int i = 0; i < cn; i++) {
        s.counters[i] = (int16_t)screen_counters_get_value(i);
    }

    /* Stay dirty if the write failed, so the next tick tries again. */
    if (!petal_kv_set(PERSIST_NS, PERSIST_KEY, &s, sizeof(s))) return;

    s_loaded = s;
    s_have   = true;
    s_dirty  = false;
}
