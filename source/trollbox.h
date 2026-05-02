// Full-screen modal trollbox viewer — chat with the rest of the crew via
// the desktop's live Supabase channel. The 3DS doesn't speak websockets,
// so the desktop hosts the connection and we proxy through HTTP polls.
//
// Modal lifecycle:
//   1. Caller invokes cog_trollbox_run(render, base_url)
//   2. We poll <base_url>trollbox every TROLLBOX_POLL_MS while the modal
//      is open; results render on the top screen as a scrolling chat log.
//   3. Y opens the keyboard composer; submitted text POSTs to /trollbox/send.
//   4. B closes the modal.
//
// Nick is persisted in sdmc:/3ds/cog-3ds/trollbox-nick.txt so the user
// only types it once. Default is "3ds".

#ifndef COG_TROLLBOX_H
#define COG_TROLLBOX_H

#include "render.h"
#include <stdbool.h>

#define TROLLBOX_NICK_MAX     24
#define TROLLBOX_MAX_MESSAGES 80
#define TROLLBOX_POLL_MS      2500

void cog_trollbox_run(CogRender *r, const char *base_url);

// Online count exposed for the canvas badge — caller polls /trollbox once
// when synthesizing the panel card and passes the count to the badge.
// Returns -1 if the desktop panel is offline.
int cog_trollbox_fetch_online_count(const char *base_url);

#endif
