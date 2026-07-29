/*
 * counter.h — a small, self-contained counter model.
 *
 * Every countable thing in the app (life, commander damage, poison, monarch,
 * day/night, ...) is expressed as a `counter_t`. Screens only ever call the
 * generic operations below, so a brand-new "custom counter" is added by
 * declaring another `counter_t` — no screen or model code needs restructuring.
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

typedef struct {
    const char *name;             /**< Human label, e.g. "Poison". */
    counter_type_t type;
    int value;
    int min;
    int max;
    bool wrap;                    /**< Wrap around at the bounds instead of clamping. */
    const char *const *labels;    /**< Optional; indexed by (value - min). NULL => numeric. */
} counter_t;

/** Step the value up/down by one, honouring bounds and `wrap`. */
void counter_increment(counter_t *c);
void counter_decrement(counter_t *c);

/** True when the value sits at the configured bound (ignores wrap). */
bool counter_at_min(const counter_t *c);
bool counter_at_max(const counter_t *c);

/** Render the current value into `buf` ("40", "ON", "Night", ...). */
void counter_value_text(const counter_t *c, char *buf, size_t buf_len);

#endif /* LOTUS_COUNTER_H */
