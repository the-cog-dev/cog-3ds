#include "http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Initial response buffer size — grown dynamically if the response is bigger.
#define INITIAL_BUF_SIZE 8192
#define MAX_BUF_SIZE     (1024 * 1024)  // 1MB ceiling, generous for /state JSON

Result cog_http_init(void) {
    // 0 = use default shared buffer size. Plenty for small JSON responses.
    return httpcInit(0);
}

void cog_http_exit(void) {
    httpcExit();
}

// Shared GET/POST core. method = HTTPC_METHOD_GET or HTTPC_METHOD_POST.
// body_json may be NULL (for GET).
static int do_request(HTTPC_RequestMethod method, const char *url,
                      const char *body_json, char **out_body, size_t *out_len) {
    *out_body = NULL;
    *out_len = 0;

    httpcContext ctx;
    Result rc;
    int status_code = -1;

    // 1 = use default chunk size for streaming download
    rc = httpcOpenContext(&ctx, method, url, 1);
    if (R_FAILED(rc)) return -1;

    // Identify ourselves so the server logs are useful
    httpcAddRequestHeaderField(&ctx, "User-Agent", "TheCog3DS/0.2");
    httpcSetKeepAlive(&ctx, HTTPC_KEEPALIVE_DISABLED);

    if (method == HTTPC_METHOD_POST && body_json != NULL) {
        httpcAddRequestHeaderField(&ctx, "Content-Type", "application/json");
        httpcAddPostDataRaw(&ctx, (u32 *)body_json, strlen(body_json));
    }


    rc = httpcBeginRequest(&ctx);
    if (R_FAILED(rc)) { httpcCloseContext(&ctx); return -2; }

    u32 status32 = 0;
    // Use timeout variant (10 seconds) — the non-timeout version can
    // hang indefinitely if Connection: close arrives before the status
    // line is fully parsed (common with Cloudflare Worker proxies).
    rc = httpcGetResponseStatusCodeTimeout(&ctx, &status32, 10000000000ULL);
    if (R_FAILED(rc)) { httpcCloseContext(&ctx); return -3; }
    status_code = (int)status32;

    // Stream the response body into a growing buffer
    char *buf = (char *)malloc(INITIAL_BUF_SIZE);
    if (!buf) { httpcCloseContext(&ctx); return -4; }
    size_t buf_cap = INITIAL_BUF_SIZE;
    size_t buf_len = 0;

    while (true) {
        // Make sure we have at least 4KB of free space to read into
        if (buf_cap - buf_len < 4096) {
            size_t new_cap = buf_cap * 2;
            if (new_cap > MAX_BUF_SIZE) new_cap = MAX_BUF_SIZE;
            if (new_cap == buf_cap) {  // already at ceiling
                free(buf); httpcCloseContext(&ctx); return -5;
            }
            char *new_buf = (char *)realloc(buf, new_cap);
            if (!new_buf) { free(buf); httpcCloseContext(&ctx); return -6; }
            buf = new_buf;
            buf_cap = new_cap;
        }
        u32 read = 0;
        rc = httpcDownloadData(&ctx, (u8 *)buf + buf_len, buf_cap - buf_len - 1, &read);
        buf_len += read;
        if (rc == 0) break;  // done
        if (rc != (Result)HTTPC_RESULTCODE_DOWNLOADPENDING) {
            // Real error
            free(buf); httpcCloseContext(&ctx); return -7;
        }
        // else: more data pending, loop
    }
    buf[buf_len] = '\0';

    httpcCloseContext(&ctx);

    *out_body = buf;
    *out_len = buf_len;
    return status_code;
}

int cog_http_get(const char *url, char **out_body, size_t *out_len) {
    return do_request(HTTPC_METHOD_GET, url, NULL, out_body, out_len);
}

int cog_http_post_json(const char *url, const char *body_json, char **out_body, size_t *out_len) {
    return do_request(HTTPC_METHOD_POST, url, body_json, out_body, out_len);
}
