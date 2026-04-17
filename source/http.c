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

// URL-encode a string for use in query parameters.
// Only unreserved chars (A-Z a-z 0-9 - _ . ~) pass through unencoded.
static void url_encode(const char *src, char *dst, size_t dst_size) {
    static const char hex[] = "0123456789ABCDEF";
    size_t di = 0;
    for (int i = 0; src[i] && di + 3 < dst_size; i++) {
        unsigned char c = (unsigned char)src[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[di++] = c;
        } else {
            dst[di++] = '%';
            dst[di++] = hex[(c >> 4) & 0xF];
            dst[di++] = hex[c & 0xF];
        }
    }
    dst[di] = '\0';
}

// Detect if a URL goes through the 3ds.thecog.dev proxy.
// If so, convert POST to GET via the /gp/ (GET-POST) endpoint.
// Cloudflare Workers POST responses are fundamentally incompatible
// with libctru's httpc — GET responses work fine.
static bool is_proxy_url(const char *url) {
    return strstr(url, "3ds.thecog.dev/p/") != NULL;
}

// Convert a proxy POST URL to a GET-based POST URL.
// Input:  http://3ds.thecog.dev/p/CODE/some/path
// Output: http://3ds.thecog.dev/p/CODE/gp/some/path?_body=ENCODED
static int do_proxy_post_as_get(const char *url, const char *body_json,
                                char **out_body, size_t *out_len) {
    // Find "/p/CODE/" in the URL
    const char *p_start = strstr(url, "/p/");
    if (!p_start) return -1;

    // Find the end of "/p/CODE/"
    const char *code_start = p_start + 3;  // skip "/p/"
    const char *code_end = strchr(code_start, '/');
    if (!code_end) return -1;
    code_end++;  // include the trailing '/'

    // Everything after "/p/CODE/" is the path
    const char *path = code_end;

    // URL-encode the JSON body
    char encoded_body[2048];
    url_encode(body_json ? body_json : "{}", encoded_body, sizeof(encoded_body));

    // Build the GET URL: replace "/p/CODE/path" with "/p/CODE/gp/path?_body=ENCODED"
    char get_url[2048];
    // Copy everything up to and including "/p/CODE/"
    size_t prefix_len = (size_t)(code_end - url);
    if (prefix_len >= sizeof(get_url) - 1) return -1;
    memcpy(get_url, url, prefix_len);
    get_url[prefix_len] = '\0';

    // Append "gp/PATH?_body=ENCODED"
    snprintf(get_url + prefix_len, sizeof(get_url) - prefix_len,
             "gp/%s?_body=%s", path, encoded_body);

    return do_request(HTTPC_METHOD_GET, get_url, NULL, out_body, out_len);
}

int cog_http_post_json(const char *url, const char *body_json, char **out_body, size_t *out_len) {
    // Route through GET-based proxy if going through 3ds.thecog.dev
    if (is_proxy_url(url)) {
        return do_proxy_post_as_get(url, body_json, out_body, out_len);
    }
    return do_request(HTTPC_METHOD_POST, url, body_json, out_body, out_len);
}
