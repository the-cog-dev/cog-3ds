// Persisted config — stores the LAN URL the user pasted/scanned.
// File lives on SD card at sdmc:/3ds/cog-3ds/config.txt.
//
// Format: a single line of text — the full Remote View URL like
//   http://192.168.2.10:54321/r/<token>/
//
// We could use JSON later if more fields are needed.

#ifndef COG_CONFIG_H
#define COG_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#define COG_URL_MAX 512
#define COG_URL_HISTORY 3

typedef struct {
    char urls[COG_URL_HISTORY][COG_URL_MAX];
    int count;  // how many valid URLs (0..3)
} CogUrlHistory;

// Load URL history (up to 3 lines from config.txt). Returns count of valid URLs.
int cog_config_load_history(CogUrlHistory *hist);

// Load just the most recent URL (backward compat). Returns true if valid.
bool cog_config_load(char *out_url, size_t out_size);

// Save url to disk — pushes to top of history, shifts others down,
// drops the 4th. Creates parent dirs as needed. Returns true on success.
bool cog_config_save(const char *url);

#endif
