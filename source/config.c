#include "config.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define CONFIG_DIR  "sdmc:/3ds/cog-3ds"
#define CONFIG_PATH "sdmc:/3ds/cog-3ds/config.txt"

static bool is_valid_url(const char *url, size_t len) {
    if (len < 12) return false;
    return strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0;
}

static void strip_trailing(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' '))
        s[--n] = '\0';
}

int cog_config_load_history(CogUrlHistory *hist) {
    memset(hist, 0, sizeof(*hist));
    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f) return 0;

    for (int i = 0; i < COG_URL_HISTORY; i++) {
        if (!fgets(hist->urls[i], COG_URL_MAX, f)) break;
        strip_trailing(hist->urls[i]);
        if (is_valid_url(hist->urls[i], strlen(hist->urls[i]))) {
            hist->count++;
        } else {
            hist->urls[i][0] = '\0';
            break;
        }
    }
    fclose(f);
    return hist->count;
}

bool cog_config_load(char *out_url, size_t out_size) {
    if (!out_url || out_size == 0) return false;
    out_url[0] = '\0';

    CogUrlHistory hist;
    if (cog_config_load_history(&hist) == 0) return false;

    strncpy(out_url, hist.urls[0], out_size - 1);
    out_url[out_size - 1] = '\0';
    return true;
}

bool cog_config_save(const char *url) {
    if (!url || !*url) return false;

    // Load existing history
    CogUrlHistory hist;
    cog_config_load_history(&hist);

    // If this URL is already in history, move it to top
    int existing = -1;
    for (int i = 0; i < hist.count; i++) {
        if (strcmp(hist.urls[i], url) == 0) { existing = i; break; }
    }

    // Build new history: new URL at top, shift others down
    CogUrlHistory new_hist;
    memset(&new_hist, 0, sizeof(new_hist));
    strncpy(new_hist.urls[0], url, COG_URL_MAX - 1);
    new_hist.count = 1;

    for (int i = 0; i < hist.count && new_hist.count < COG_URL_HISTORY; i++) {
        if (i == existing) continue;  // skip duplicate
        strncpy(new_hist.urls[new_hist.count], hist.urls[i], COG_URL_MAX - 1);
        new_hist.count++;
    }

    // Write to file
    mkdir(CONFIG_DIR, 0777);
    FILE *f = fopen(CONFIG_PATH, "w");
    if (!f) return false;
    for (int i = 0; i < new_hist.count; i++) {
        fputs(new_hist.urls[i], f);
        fputc('\n', f);
    }
    fclose(f);
    return true;
}
