#include "action_menu.h"
#include "theme.h"
#include <3ds.h>
#include <string.h>
#include <stdio.h>

// ─── Layout constants ────────────────────────────────────────────────────────
#define BOT_W       320
#define BOT_H       240
#define TOP_W       400
#define TOP_H       240

#define MENU_W      200
#define ITEM_H      28
#define BORDER_PX   2
#define RADIUS      5.0f
#define HEADER_H    30   // space for card-type label inside box

// ─── Menu item definition ─────────────────────────────────────────────────────
typedef struct {
    const char *label;
    MenuAction  action;
} MenuItem;

// ─── Per-type item tables ─────────────────────────────────────────────────────
static const MenuItem AGENT_ITEMS[] = {
    { "Message",     ACTION_MESSAGE     },
    { "View Output", ACTION_VIEW_OUTPUT },
    { "Kill",        ACTION_KILL        },
};
static const int AGENT_ITEM_COUNT = 3;

static const MenuItem PINBOARD_ITEMS[] = {
    { "Create Task",   ACTION_CREATE_TASK   },
    { "Claim Task",    ACTION_CLAIM_TASK    },
    { "Complete Task", ACTION_COMPLETE_TASK },
    { "Abandon Task",  ACTION_ABANDON_TASK  },
};
static const int PINBOARD_ITEM_COUNT = 4;

static const MenuItem INFO_ITEMS[] = {
    { "Create Note", ACTION_CREATE_NOTE },
    { "Delete Note", ACTION_DELETE_NOTE },
};
static const int INFO_ITEM_COUNT = 2;

static const MenuItem NONE_ITEMS[] = {
    { "Spawn Agent", ACTION_SPAWN },
};
static const int NONE_ITEM_COUNT = 1;

// ─── Helpers ──────────────────────────────────────────────────────────────────
static const char *card_type_label(CardType t) {
    switch (t) {
        case CARD_TYPE_AGENT:    return "Agent";
        case CARD_TYPE_PINBOARD: return "Pinboard";
        case CARD_TYPE_INFO:     return "Info";
        case CARD_TYPE_NONE:     return "Canvas";
        default:                 return "";
    }
}

static void get_items(CardType t,
                      const MenuItem **items_out,
                      int             *count_out) {
    switch (t) {
        case CARD_TYPE_AGENT:
            *items_out = AGENT_ITEMS;
            *count_out = AGENT_ITEM_COUNT;
            break;
        case CARD_TYPE_PINBOARD:
            *items_out = PINBOARD_ITEMS;
            *count_out = PINBOARD_ITEM_COUNT;
            break;
        case CARD_TYPE_INFO:
            *items_out = INFO_ITEMS;
            *count_out = INFO_ITEM_COUNT;
            break;
        case CARD_TYPE_NONE:
        default:
            *items_out = NONE_ITEMS;
            *count_out = NONE_ITEM_COUNT;
            break;
    }
}

// ─── Drawing ─────────────────────────────────────────────────────────────────

// Draw the top screen: "Actions" header, card name, and control hints.
static void draw_top_screen(CogRender *r, CardType card_type, const char *card_name) {
    cog_render_target_top(r, THEME_BG_DARK);

    // Header bar
    cog_render_rect(0, 0, TOP_W, 28, THEME_BG_CANVAS);
    cog_render_rect(0, 27, TOP_W, 1, THEME_DIVIDER);
    cog_render_text(r, "Actions", 12, 5, THEME_FONT_HEADER, THEME_GOLD);

    // Card type badge
    char badge[32];
    snprintf(badge, sizeof(badge), "[%s]", card_type_label(card_type));
    cog_render_text_right(r, badge, TOP_W - 12, 10, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);

    // Card name
    if (card_name && *card_name) {
        cog_render_text(r, card_name, 12, 44, THEME_FONT_HEADER, THEME_TEXT_PRIMARY);
    } else {
        cog_render_text(r, "(no name)", 12, 44, THEME_FONT_HEADER, THEME_TEXT_DIMMED);
    }

    // Divider below name
    cog_render_rect(12, 72, TOP_W - 24, 1, THEME_DIVIDER);

    // Control hints
    float hy = TOP_H - 22;
    cog_render_rect(0, hy - 2, TOP_W, 1, THEME_DIVIDER);
    cog_render_text(r,
        "[Up/Dn] navigate  [A] confirm  [B] dismiss  [touch] tap to pick",
        12, hy + 2, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
}

// Draw the bottom screen: dim overlay + menu box.
static void draw_bottom_screen(CogRender *r,
                                CardType card_type,
                                const MenuItem *items,
                                int item_count,
                                int selected) {
    cog_render_target_bottom(r, THEME_BG_DARK);

    // Semi-transparent overlay covering the whole bottom screen
    cog_render_rect(0, 0, BOT_W, BOT_H, C2D_Color32(0x0d, 0x0d, 0x0d, 0xcc));

    // Calculate box height: border×2 + header + items
    int box_h = BORDER_PX * 2 + HEADER_H + item_count * ITEM_H;

    // Center the box
    float box_x = (BOT_W - MENU_W) / 2.0f;
    float box_y = (BOT_H - box_h) / 2.0f;

    // Outer gold border (rounded rect)
    cog_render_rounded_rect(box_x, box_y, MENU_W, box_h, RADIUS, THEME_GOLD_DIM);

    // Inner dark fill
    cog_render_rounded_rect(
        box_x + BORDER_PX, box_y + BORDER_PX,
        MENU_W - BORDER_PX * 2, box_h - BORDER_PX * 2,
        RADIUS, THEME_BG_DARK);

    // Header label (card type) — centered in the header area
    const char *type_label = card_type_label(card_type);
    float label_w = cog_render_text_width(r, type_label, THEME_FONT_LABEL);
    float label_x = box_x + (MENU_W - label_w) / 2.0f;
    float label_y = box_y + BORDER_PX + (HEADER_H - 16) / 2.0f;
    cog_render_text(r, type_label, label_x, label_y, THEME_FONT_LABEL, THEME_GOLD);

    // Divider below header
    float div_y = box_y + BORDER_PX + HEADER_H - 1;
    cog_render_rect(box_x + BORDER_PX, div_y,
                    MENU_W - BORDER_PX * 2, 1, THEME_DIVIDER);

    // Menu items
    float item_x = box_x + BORDER_PX;
    float item_w  = MENU_W - BORDER_PX * 2;

    for (int i = 0; i < item_count; i++) {
        float iy = box_y + BORDER_PX + HEADER_H + i * ITEM_H;

        if (i == selected) {
            // Highlighted background
            cog_render_rounded_rect(item_x + 2, iy + 2,
                                    item_w - 4, ITEM_H - 4,
                                    3.0f, THEME_GOLD_DIM);
        }

        u32 text_col = (i == selected) ? THEME_TEXT_PRIMARY : THEME_TEXT_DIMMED;

        // Vertically center text within the item
        float ty = iy + (ITEM_H - 16) / 2.0f;
        // Indent text slightly from the left edge
        cog_render_text(r, items[i].label, item_x + 10, ty,
                        THEME_FONT_LABEL, text_col);
    }
}

// ─── Public entry point ───────────────────────────────────────────────────────
MenuAction cog_action_menu(CogRender *r, CardType card_type, const char *card_name) {
    const MenuItem *items  = NULL;
    int             count  = 0;
    get_items(card_type, &items, &count);

    if (count == 0) return ACTION_NONE;

    int selected = 0;

    // Pre-compute box geometry for touch hit-testing
    int   box_h = BORDER_PX * 2 + HEADER_H + count * ITEM_H;
    float box_x = (BOT_W - MENU_W) / 2.0f;
    float box_y = (BOT_H - box_h) / 2.0f;
    float item_x = box_x + BORDER_PX;
    float item_w  = MENU_W - BORDER_PX * 2;

    while (aptMainLoop()) {
        hidScanInput();
        u32 kd = hidKeysDown();

        // D-pad navigation
        if (kd & KEY_DDOWN) {
            selected = (selected + 1) % count;
        }
        if (kd & KEY_DUP) {
            selected = (selected - 1 + count) % count;
        }

        // Confirm
        if (kd & KEY_A) {
            return items[selected].action;
        }

        // Dismiss
        if (kd & KEY_B) {
            return ACTION_NONE;
        }

        // Touch — check if tap lands on an item row
        if (kd & KEY_TOUCH) {
            touchPosition tp;
            hidTouchRead(&tp);
            float tx = (float)tp.px;
            float ty = (float)tp.py;

            // Check if touch is within the item column
            if (tx >= item_x && tx <= item_x + item_w) {
                float items_top = box_y + BORDER_PX + HEADER_H;
                for (int i = 0; i < count; i++) {
                    float iy = items_top + i * ITEM_H;
                    if (ty >= iy && ty < iy + ITEM_H) {
                        // Tap on already-selected item confirms it; otherwise select it
                        if (i == selected) {
                            return items[i].action;
                        } else {
                            selected = i;
                        }
                        break;
                    }
                }
            }
        }

        // Draw frame
        cog_render_frame_begin(r);
        draw_top_screen(r, card_type, card_name);
        draw_bottom_screen(r, card_type, items, count, selected);
        cog_render_frame_end(r);
    }

    return ACTION_NONE;
}
