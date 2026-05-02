#ifndef COG_ACTION_MENU_H
#define COG_ACTION_MENU_H

#include "render.h"
#include <stdbool.h>

typedef enum {
    CARD_TYPE_AGENT,
    CARD_TYPE_PINBOARD,
    CARD_TYPE_INFO,
    CARD_TYPE_SCHEDULE,
    CARD_TYPE_INBOX,
    CARD_TYPE_NONE
} CardType;

typedef enum {
    ACTION_NONE,
    ACTION_MESSAGE,
    ACTION_VIEW_OUTPUT,
    ACTION_KILL,
    ACTION_CREATE_TASK,
    ACTION_CLAIM_TASK,
    ACTION_COMPLETE_TASK,
    ACTION_ABANDON_TASK,
    ACTION_CREATE_NOTE,
    ACTION_DELETE_NOTE,
    ACTION_PAUSE_SCHEDULE,
    ACTION_RESUME_SCHEDULE,
    ACTION_SPAWN,
    // Inbox panel: opens the inbox modal viewer (cog_inbox_run).
    ACTION_OPEN_INBOX
} MenuAction;

// Show a context-sensitive action menu overlay. Blocks until the user
// selects an action (A), dismisses (B), or the menu otherwise resolves.
// Returns the chosen MenuAction, or ACTION_NONE if dismissed.
MenuAction cog_action_menu(CogRender *r, CardType card_type, const char *card_name);

#endif
