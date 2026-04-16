// Network URL receiver — listens for a URL sent from the desktop Cog
// app over the local network. The 3DS shows its IP, user clicks "Send
// to 3DS" on the desktop, URL arrives instantly. No QR, no typing.

#ifndef COG_NET_RECV_H
#define COG_NET_RECV_H

#include "render.h"
#include <stdbool.h>
#include <stddef.h>

// Listen for a URL over the network. Shows the 3DS's IP on screen
// while waiting. Returns true if a URL was received (copied into
// out_url), false if the user cancelled with B.
bool cog_net_recv(CogRender *render, char *out_url, size_t out_size);

#endif
