#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <cairo/cairo-xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "status.h"

#define BAR_HEIGHT 20
#define MAX_BUF 1024

BarState state;

void redraw_bar(cairo_t *cr, int width, int height)
{
    cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
    cairo_paint(cr);

    int x = 10;
    int box_w = 30;

    for (int i = 0; i < MAX_WS; i++) {
        Workspace *ws = &state.ws[i];

        // Highlight if focused
        if (ws->num == state.focused) {
            cairo_set_source_rgb(cr, 0.3, 0.3, 0.8);
            cairo_rectangle(cr, x, 0, box_w, height);
            cairo_fill(cr);
        }

        // Draw workspace number
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", ws->num);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_move_to(cr, x + 8, height - 6);
        cairo_show_text(cr, buf);

        x += box_w + 5;
    }
}

int main()
{
    // --- SOCKET SETUP ---
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, "/tmp/shedwm_bar.sock");

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        return 1;
    }

    // --- X11 SETUP ---
    Display *d = XOpenDisplay(NULL);
    if (!d) return 1;

    int s = DefaultScreen(d);
    int width = DisplayWidth(d, s);
    int height = BAR_HEIGHT;

    Window w = XCreateSimpleWindow(d, RootWindow(d, s), 0, 0, width, height, 0, 0, 0);

    // Dock properties
    Atom type = XInternAtom(d, "_NET_WM_WINDOW_TYPE", False);
    Atom dock = XInternAtom(d, "_NET_WM_WINDOW_TYPE_DOCK", False);
    XChangeProperty(d, w, type, XA_ATOM, 32, PropModeReplace, (unsigned char *)&dock, 1);

    // Avoid being covered (partial strut)
    Atom strut = XInternAtom(d, "_NET_WM_STRUT_PARTIAL", False);
    long strut_data[12] = {0};
    strut_data[2] = height;  // top strut
    strut_data[9] = width;   // top right
    XChangeProperty(d, w, strut, XA_CARDINAL, 32, PropModeReplace, (unsigned char*)strut_data, 12);

    XSelectInput(d, w, ExposureMask);
    XMapWindow(d, w);

    cairo_surface_t *surf = cairo_xlib_surface_create(d, w, DefaultVisual(d, s), width, height);
    cairo_t *cr = cairo_create(surf);

    fd_set fds;
    int xfd = ConnectionNumber(d);

    while (1) {
        FD_ZERO(&fds);
        FD_SET(xfd, &fds);
        FD_SET(sock, &fds);
        int maxfd = (xfd > sock ? xfd : sock) + 1;

        if (select(maxfd, &fds, NULL, NULL, NULL) < 0) {
            perror("select");
            break;
        }

        // --- X11 EVENTS ---
        if (FD_ISSET(xfd, &fds)) {
            while (XPending(d)) {
                XEvent e;
                XNextEvent(d, &e);

                if (e.type == Expose && e.xexpose.count == 0) {
                    redraw_bar(cr, width, height);
                    cairo_surface_flush(surf);
                    XFlush(d);
                }
            }
        }

        // --- SOCKET EVENTS ---
        if (FD_ISSET(sock, &fds)) {
            static char buf[MAX_BUF];
            static int buf_len = 0;
            
            // Append new data to buffer
            int len = read(sock, buf + buf_len, sizeof(buf) - buf_len - 1);
            if (len <= 0) continue; 
            buf_len += len;
            buf[buf_len] = '\0';

            // Check if we have a full line (newline at end)
            char *newline = strrchr(buf, '\n'); 
            if (newline) {
                *newline = '\0'; // Terminate the string at the last newline
                
                // If there are multiple updates, parse the last one (most recent state)
                char *last_json = strrchr(buf, '\n');
                if (last_json) last_json++; // skip the newline
                else last_json = buf;

                parse_status_json(last_json, &state);
                
                redraw_bar(cr, width, height);
                cairo_surface_flush(surf);
                XFlush(d);
                
                // Reset buffer
                buf_len = 0;
                memset(buf, 0, sizeof(buf));
            }
        }
    }

    close(sock);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    XCloseDisplay(d);
    return 0;
}