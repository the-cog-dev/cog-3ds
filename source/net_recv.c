#include "net_recv.h"
#include "theme.h"

#include <3ds.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <malloc.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define RECV_PORT 8336
#define SOC_BUFSIZE 0x100000  // 1MB — standard for 3DS SOC service

// Get the 3DS's local IP address as a string.
static bool get_local_ip(char *out, size_t out_size) {
    u32 ip = gethostid();
    if (ip == 0 || ip == 0x7f000001) return false;
    unsigned char *b = (unsigned char *)&ip;
    snprintf(out, out_size, "%d.%d.%d.%d", b[0], b[1], b[2], b[3]);
    return true;
}

bool cog_net_recv(CogRender *render, char *out_url, size_t out_size) {
    if (!out_url || out_size == 0) return false;
    out_url[0] = '\0';

    // SOC service needs a page-aligned buffer
    u32 *soc_buf = (u32 *)memalign(0x1000, SOC_BUFSIZE);
    if (!soc_buf) {
        if (render) {
            cog_render_frame_begin(render);
            cog_render_target_top(render, THEME_BG_DARK);
            cog_render_text(render, "memalign failed", 80, 100, THEME_FONT_HEADER, THEME_STATUS_DISCONNECTED);
            cog_render_target_bottom(render, THEME_BG_CANVAS);
            cog_render_frame_end(render);
            for (int f = 0; f < 120; f++) gspWaitForVBlank();
        }
        return false;
    }

    Result soc_rc = socInit(soc_buf, SOC_BUFSIZE);
    if (R_FAILED(soc_rc)) {
        if (render) {
            char msg[64];
            snprintf(msg, sizeof(msg), "socInit failed: %08lX", soc_rc);
            cog_render_frame_begin(render);
            cog_render_target_top(render, THEME_BG_DARK);
            cog_render_text(render, msg, 40, 100, THEME_FONT_LABEL, THEME_STATUS_DISCONNECTED);
            cog_render_text(render, "Is WiFi connected?", 40, 130, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
            cog_render_target_bottom(render, THEME_BG_CANVAS);
            cog_render_frame_end(render);
            for (int f = 0; f < 120; f++) gspWaitForVBlank();
        }
        free(soc_buf);
        return false;
    }

    char local_ip[32] = "?.?.?.?";
    get_local_ip(local_ip, sizeof(local_ip));

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        if (render) {
            cog_render_frame_begin(render);
            cog_render_target_top(render, THEME_BG_DARK);
            cog_render_text(render, "socket() failed", 80, 100, THEME_FONT_HEADER, THEME_STATUS_DISCONNECTED);
            cog_render_target_bottom(render, THEME_BG_CANVAS);
            cog_render_frame_end(render);
            for (int f = 0; f < 120; f++) gspWaitForVBlank();
        }
        socExit(); free(soc_buf); return false;
    }

    int one = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(RECV_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        if (render) {
            cog_render_frame_begin(render);
            cog_render_target_top(render, THEME_BG_DARK);
            cog_render_text(render, "bind() failed — port in use?", 40, 100,
                            THEME_FONT_LABEL, THEME_STATUS_DISCONNECTED);
            cog_render_target_bottom(render, THEME_BG_CANVAS);
            cog_render_frame_end(render);
            for (int f = 0; f < 120; f++) gspWaitForVBlank();
        }
        close(server_fd); socExit(); free(soc_buf); return false;
    }
    if (listen(server_fd, 1) < 0) {
        if (render) {
            cog_render_frame_begin(render);
            cog_render_target_top(render, THEME_BG_DARK);
            cog_render_text(render, "listen() failed", 80, 100,
                            THEME_FONT_LABEL, THEME_STATUS_DISCONNECTED);
            cog_render_target_bottom(render, THEME_BG_CANVAS);
            cog_render_frame_end(render);
            for (int f = 0; f < 120; f++) gspWaitForVBlank();
        }
        close(server_fd); socExit(); free(soc_buf); return false;
    }

    // Non-blocking so we can poll for B button
    int flags = fcntl(server_fd, F_GETFL, 0);
    if (flags < 0 || fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        if (render) {
            cog_render_frame_begin(render);
            cog_render_target_top(render, THEME_BG_DARK);
            cog_render_text(render, "fcntl() failed — no non-blocking", 20, 100,
                            THEME_FONT_LABEL, THEME_STATUS_DISCONNECTED);
            cog_render_target_bottom(render, THEME_BG_CANVAS);
            cog_render_frame_end(render);
            for (int f = 0; f < 120; f++) gspWaitForVBlank();
        }
        close(server_fd); socExit(); free(soc_buf); return false;
    }

    bool success = false;
    char status[64] = "Waiting for connection...";

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_B) break;

        // Try to accept a connection
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

        if (client_fd >= 0) {
            // Read the URL (first line of data)
            char buf[512] = {0};
            // Brief blocking read — client sends URL immediately
            fcntl(client_fd, F_SETFL, fcntl(client_fd, F_GETFL, 0) & ~O_NONBLOCK);

            int n = recv(client_fd, buf, sizeof(buf) - 1, 0);
            if (n > 0) {
                buf[n] = '\0';
                // Strip trailing whitespace/newlines
                while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' || buf[n-1] == ' '))
                    buf[--n] = '\0';
                // Validate it looks like a URL
                if (n > 10 && strncmp(buf, "http", 4) == 0) {
                    strncpy(out_url, buf, out_size - 1);
                    out_url[out_size - 1] = '\0';
                    success = true;
                    snprintf(status, sizeof(status), "Received!");
                } else {
                    snprintf(status, sizeof(status), "Invalid data received");
                }
            }
            // Send a simple response so curl/browser sees success
            const char *resp = success
                ? "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\nURL saved!\n"
                : "HTTP/1.0 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nSend a URL starting with http\n";
            send(client_fd, resp, strlen(resp), 0);
            close(client_fd);

            if (success) {
                // Show confirmation briefly
                if (render) {
                    cog_render_frame_begin(render);
                    cog_render_target_top(render, THEME_BG_DARK);
                    cog_render_text(render, "URL Received!", 100, 100,
                                    THEME_FONT_HEADER, THEME_GOLD);
                    cog_render_text(render, out_url, 12, 150,
                                    THEME_FONT_FOOTER, THEME_TEXT_PRIMARY);
                    cog_render_target_bottom(render, THEME_BG_CANVAS);
                    cog_render_frame_end(render);
                }
                for (int f = 0; f < 60; f++) gspWaitForVBlank();
                break;
            }
        }

        // Render the listening screen
        if (render) {
            cog_render_frame_begin(render);
            cog_render_target_top(render, THEME_BG_DARK);
            cog_render_text(render, "Network Receiver", 100, 30,
                            THEME_FONT_HEADER, THEME_GOLD);
            cog_render_text(render, "On your PC, run:", 12, 80,
                            THEME_FONT_LABEL, THEME_TEXT_DIMMED);

            char cmd[128];
            snprintf(cmd, sizeof(cmd), "curl %s:%d -d \"YOUR_URL\"",
                     local_ip, RECV_PORT);
            cog_render_text(render, cmd, 12, 105,
                            THEME_FONT_FOOTER, THEME_TEXT_PRIMARY);

            cog_render_text(render, "Or use Send to 3DS in Cog settings.",
                            12, 140, THEME_FONT_LABEL, THEME_TEXT_DIMMED);

            char ip_line[64];
            snprintf(ip_line, sizeof(ip_line), "3DS IP: %s   Port: %d",
                     local_ip, RECV_PORT);
            cog_render_text(render, ip_line, 12, 180,
                            THEME_FONT_LABEL, THEME_GOLD);

            cog_render_text(render, status, 12, 210,
                            THEME_FONT_FOOTER, THEME_TEXT_DIMMED);

            cog_render_target_bottom(render, THEME_BG_CANVAS);
            cog_render_text(render, "Listening...", 100, 100,
                            THEME_FONT_HEADER, THEME_GOLD);
            cog_render_text(render, "[B] cancel", 110, 160,
                            THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
            cog_render_frame_end(render);
        }

        gspWaitForVBlank();
    }

    close(server_fd);
    socExit();
    free(soc_buf);
    return success;
}
