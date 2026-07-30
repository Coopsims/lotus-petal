#include "counter.h"

#include <stdio.h>

void counter_increment(counter_t *c)
{
    if (c->value >= c->max) {
        c->value = c->wrap ? c->min : c->max;
        return;
    }
    c->value++;
}

void counter_decrement(counter_t *c)
{
    if (c->value <= c->min) {
        c->value = c->wrap ? c->max : c->min;
        return;
    }
    c->value--;
}

void counter_reset(counter_t *c)
{
    c->value = c->min;
}

bool counter_at_min(const counter_t *c)
{
    return c->value <= c->min;
}

bool counter_at_max(const counter_t *c)
{
    return c->value >= c->max;
}

void counter_value_text(const counter_t *c, char *buf, size_t buf_len)
{
    /* Commander tax stores the cast count but reads as the mana it costs you, so
     * the derivation belongs here rather than in whichever screen draws it. */
    if (c->role == COUNTER_ROLE_COMMANDER_TAX) {
        snprintf(buf, buf_len, "+%d", c->value * COUNTER_TAX_PER_CAST);
        return;
    }
    if (c->labels != NULL) {
        int idx = c->value - c->min;
        snprintf(buf, buf_len, "%s", c->labels[idx]);
        return;
    }
    snprintf(buf, buf_len, "%d", c->value);
}
