/*
 * counter.h — a small, self-contained counter model.
 *
 * Every countable thing in the app (life, commander damage, poison, monarch,
 * day/night, ...) is expressed as a `counter_t`. Screens only ever call the
 * generic operations below, so a brand-new "custom counter" is added by
 * declaring another `counter_t` — no screen or model code needs restructuring.
 *
 * A counter carries its own MEANING in `role` and `per_turn`. Nothing anywhere
 * may key behaviour off `name`: that field is a display label, and matching on
 * it means renaming or translating a counter silently changes the rules. (It
 * used to: poison was found with strcmp(name, "Poison"), and poison feeds a
 * lethal threshold, so a renamed label would have quietly disabled the bow-out
 * prompt with no error at all.)
 */
#ifndef LOTUS_COUNTER_H
#define LOTUS_COUNTER_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    COUNTER_TYPE_INT,    /**< Plain integer, clamped (or wrapped) to [min,max]. */
    COUNTER_TYPE_TOGGLE, /**< Boolean; rendered via `labels[0/1]` (e.g. OFF/ON). */
    COUNTER_TYPE_CYCLE,  /**< Enum 0..(max); advances through `labels[]`. */
} counter_type_t;

/**
 * What a counter means to the rest of the app, where anything outside the
 * counter itself needs to care. Add a role only when behaviour genuinely differs
 * — a counter that is just a number wants COUNTER_ROLE_PLAIN.
 */
typedef enum {
    COUNTER_ROLE_PLAIN = 0,     /**< Just a number. The default. */
    COUNTER_ROLE_TOKEN,         /**< A physical token you hold; highlighted when > 0. */
    COUNTER_ROLE_COMMANDER_TAX, /**< Counts casts; renders as the derived tax. */
    COUNTER_ROLE_POISON,        /**< Feeds a lethal threshold (see game.h). */
    COUNTER_ROLE_MONARCH,       /**< Table-wide: exactly one player holds it. */
    COUNTER_ROLE_INITIATIVE,    /**< Table-wide: exactly one player holds it. */
} counter_role_t;

/** True for the roles the whole table shares, where "on" means *this* player
 *  holds something only one player can hold. In a linked game these are driven
 *  by the shared round state rather than by the local counter value. */
static inline bool counter_role_is_table_wide(counter_role_t r)
{
    return r == COUNTER_ROLE_MONARCH || r == COUNTER_ROLE_INITIATIVE;
}

/** Commander tax is two generic mana per previous cast from the command zone, so
 *  the stored value is the cast count and the displayed value is derived. */
#define COUNTER_TAX_PER_CAST 2

/** Poison at this count is lethal. Used both as the counter's ceiling and by the
 *  elimination check, so the rule lives in exactly one place. */
#define COUNTER_POISON_LETHAL 10

typedef struct {
    const char *name;             /**< Display label only — never match on it. */
    counter_type_t type;
    counter_role_t role;          /**< Defaults to COUNTER_ROLE_PLAIN. */
    int value;
    int min;
    int max;
    bool wrap;                    /**< Wrap around at the bounds instead of clamping. */
    bool per_turn;                /**< Reset to `min` when the turn advances (e.g. Storm). */
    const char *const *labels;    /**< Optional; indexed by (value - min). NULL => numeric. */
} counter_t;

/** Step the value up/down by one, honouring bounds and `wrap`. */
void counter_increment(counter_t *c);
void counter_decrement(counter_t *c);

/** Back to the starting value. Used by both New Game and the per-turn reset. */
void counter_reset(counter_t *c);

/** True when the value sits at the configured bound (ignores wrap). */
bool counter_at_min(const counter_t *c);
bool counter_at_max(const counter_t *c);

/** Render the current value into `buf` ("40", "ON", "Night", "+4", ...).
 *  Role-derived displays (commander tax) are resolved here, so every caller
 *  renders a counter the same way. */
void counter_value_text(const counter_t *c, char *buf, size_t buf_len);

#endif /* LOTUS_COUNTER_H */
