#include "screen_manager.h"

#define MAX_SCREENS 8

typedef struct {
    const char *name;
    screen_create_fn create;
    screen_input_fn handle_input;
    lv_obj_t *root;
} screen_entry_t;

static screen_entry_t s_screens[MAX_SCREENS];
static int s_count;
static int s_current;

void screen_manager_add(const char *name, screen_create_fn create, screen_input_fn handle_input)
{
    if (s_count >= MAX_SCREENS) {
        LV_LOG_WARN("screen_manager: registry full, ignoring '%s'", name);
        return;
    }
    s_screens[s_count].name = name;
    s_screens[s_count].create = create;
    s_screens[s_count].handle_input = handle_input;
    s_screens[s_count].root = NULL;
    s_count++;
}

void screen_manager_start(void)
{
    for (int i = 0; i < s_count; i++) {
        s_screens[i].root = s_screens[i].create();
    }
    s_current = 0;
    if (s_count > 0) {
        lv_screen_load(s_screens[0].root);
    }
}

void screen_manager_next(void)
{
    if (s_count <= 1) {
        return;
    }
    s_current = (s_current + 1) % s_count;
    lv_screen_load_anim(s_screens[s_current].root,
                        LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false);
}

void screen_manager_prev(void)
{
    if (s_count <= 1) {
        return;
    }
    s_current = (s_current + s_count - 1) % s_count;
    lv_screen_load_anim(s_screens[s_current].root,
                        LV_SCR_LOAD_ANIM_MOVE_RIGHT, 250, 0, false);
}

void screen_manager_handle_input(input_event_t ev, void *user_data)
{
    LV_UNUSED(user_data);

    if (ev == INPUT_EV_NEXT_SCREEN) {
        screen_manager_next();
        return;
    }

    if (ev == INPUT_EV_PREV_SCREEN) {
        screen_manager_prev();
        return;
    }

    if (s_current < s_count && s_screens[s_current].handle_input != NULL) {
        s_screens[s_current].handle_input(ev);
    }
}
