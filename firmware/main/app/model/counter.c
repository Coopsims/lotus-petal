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
    if (c->labels != NULL) {
        int idx = c->value - c->min;
        snprintf(buf, buf_len, "%s", c->labels[idx]);
    } else {
        snprintf(buf, buf_len, "%d", c->value);
    }
}
