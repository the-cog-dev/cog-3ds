// Tiny wrapper around libctru's httpc service. Synchronous GET and POST
// helpers that return a malloc'd response body. Caller owns the buffer.
//
// This is HTTP-only for Phase 1 (LAN access mode on the Cog server). HTTPS
// support comes in Phase 4 once we wire wolfSSL or use libcurl-with-CA-bundle.

#ifndef COG_HTTP_H
#define COG_HTTP_H

#include <3ds.h>
#include <stddef.h>

// Initialize the httpc service. Must be called before any get/post.
// Returns 0 on success, non-zero result code on failure.
Result cog_http_init(void);
void   cog_http_exit(void);

// GET <url>. On success, *out_body is a malloc'd null-terminated buffer
// containing the response body (caller must free), *out_len is the body
// length, and the return value is the HTTP status code. On failure returns
// a negative number; *out_body and *out_len are set to NULL/0.
int cog_http_get(const char *url, char **out_body, size_t *out_len);

// POST JSON. Same semantics as GET. body_json is null-terminated.
int cog_http_post_json(const char *url, const char *body_json, char **out_body, size_t *out_len);

#endif
