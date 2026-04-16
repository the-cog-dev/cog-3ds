// QR code scanner — uses the outer camera (left sensor) to capture YUV422
// frames, extracts the luma plane, and feeds it to quirc for decoding.
//
// Phase 1.5: blind-aim scanner with no camera preview. User points the 3DS
// at a QR code and watches status feedback on the bottom screen. Phase 2
// will add a live camera preview via citro2d framebuffer drawing.
//
// Returns true if a QR code was successfully decoded AND its payload looks
// like a valid Cog URL (http:// prefix). The decoded URL is copied into
// out_url (caller provides a COG_URL_MAX-sized buffer).

#ifndef COG_QR_SCAN_H
#define COG_QR_SCAN_H

#include <stdbool.h>
#include <stddef.h>

// Run the QR scanner. Blocks until scan succeeds (returns true), user
// cancels with B (returns false), or an unrecoverable error occurs
// (returns false). The PrintConsole state on both screens is clobbered;
// caller should redraw after this returns.
bool cog_qr_scan(char *out_url, size_t out_size);

#endif
