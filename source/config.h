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

// Load the saved URL into out_url (size COG_URL_MAX). Returns true if a
// usable URL was loaded, false if no config exists or it's invalid.
bool cog_config_load(char *out_url, size_t out_size);

// Save url to disk. Creates parent dirs as needed. Returns true on success.
bool cog_config_save(const char *url);

#endif
