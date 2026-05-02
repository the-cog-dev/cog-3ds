// Top-screen detail view — header bar + selected agent info.
// Called once per frame after the render target switches to top.

#ifndef COG_DETAIL_H
#define COG_DETAIL_H

#include "render.h"
#include "canvas.h"

// Draw header + footer + body. card_or_null is the selected card
// or NULL for the empty state. project_name shows in the header.
// pinboard_tab: 0=open, 1=in_progress, 2=completed.
// inbox_count/unread render the preview when an inbox panel card is selected.
void detail_draw(CogRender *r, const char *project_name,
                 const Card *card_or_null, int agent_count,
                 int connection_count,
                 const void *tasks, int task_count,
                 const void *infos, int info_count,
                 const void *schedules, int schedule_count,
                 int detail_scroll, int pinboard_tab,
                 int inbox_count, int inbox_unread);

#endif
