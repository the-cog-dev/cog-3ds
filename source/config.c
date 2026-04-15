#include "config.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define CONFIG_DIR  "sdmc:/3ds/cog-3ds"
#define CONFIG_PATH "sdmc:/3ds/cog-3ds/config.txt"

bool cog_config_load(char *out_url, size_t out_size) {
    if (!out_url || out_size == 0) return false;
    out_url[0] = '\0';

    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f) return false;

    if (!fgets(out_url, (int)out_size, f)) {
        fclose(f);
        return false;
    }
    fclose(f);

    // Strip trailing newline / whitespace
    size_t n = strlen(out_url);
    while (n > 0 && (out_url[n-1] == '\n' || out_url[n-1] == '\r' || out_url[n-1] == ' ')) {
        out_url[--n] = '\0';
    }

    // Sanity: must be at least "http://x/r/y/" — accept anything starting with http://
    if (n < 12 || strncmp(out_url, "http://", 7) != 0) {
        out_url[0] = '\0';
        return false;
    }
    return true;
}

bool cog_config_save(const char *url) {
    if (!url || !*url) return false;
    // mkdir is no-op if exists; ignore error
    mkdir(CONFIG_DIR, 0777);
    FILE *f = fopen(CONFIG_PATH, "w");
    if (!f) return false;
    fputs(url, f);
    fputc('\n', f);
    fclose(f);
    return true;
}
