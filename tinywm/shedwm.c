#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>

#define MAX_WINDOWS 50
#define MAX_WORKSPACES 9
#define MOD Mod4Mask

#define KEY_RETURN 36
#define KEY_Q 24
#define KEY_R 27
#define KEY_D 40
#define KEY_1 10
#define KEY_9 18

typedef struct {
    Window win;
} Client;

Client workspaces[MAX_WORKSPACES][MAX_WINDOWS];
int win_counts[MAX_WORKSPACES] = {0};
int curr = 0;

Display *dpy;
Window root;
Atom wm_delete;
char *wm_path;

/* ---------- BAR IPC ---------- */

int bar_server = -1;
int bar_client = -1;

void bar_ipc_init()
{
    struct sockaddr_un addr = {0};

    bar_server = socket(AF_UNIX, SOCK_STREAM, 0);
    if (bar_server < 0) return;

    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, "/tmp/shedwm_bar.sock");

    unlink(addr.sun_path);
    bind(bar_server, (struct sockaddr*)&addr, sizeof(addr));
    listen(bar_server, 1);

    fcntl(bar_server, F_SETFL, O_NONBLOCK);
}

void bar_try_accept()
{
    if (bar_client >= 0) return;
    bar_client = accept(bar_server, NULL, NULL);
}

void bar_send_update()
{
    if (bar_client < 0) return;

    char json[512];
    int len = 0;

    len += sprintf(json + len, "{ \"focused\": %d, \"workspaces\": [", curr + 1);

    for (int i = 0; i < MAX_WORKSPACES; i++) {
        int occupied = win_counts[i] > 0;
        len += sprintf(json + len,
            "{\"num\":%d,\"occupied\":%s}%s",
            i + 1,
            occupied ? "true" : "false",
            (i < MAX_WORKSPACES - 1) ? "," : ""
        );
    }

    len += sprintf(json + len, "] }\n");

    if (write(bar_client, json, len) <= 0) {
        close(bar_client);
        bar_client = -1;
    }
}

/* ---------- WINDOW MANAGEMENT ---------- */

int supports_protocol(Window w, Atom proto)
{
    Atom *protos;
    int n, found = 0;

    if (XGetWMProtocols(dpy, w, &protos, &n)) {
        while (!found && n--)
            found = protos[n] == proto;
        XFree(protos);
    }
    return found;
}

void tile(int ws)
{
    int n = win_counts[ws];
    if (n == 0) return;

    int sw = DisplayWidth(dpy, DefaultScreen(dpy));
    int sh = DisplayHeight(dpy, DefaultScreen(dpy));

    if (n == 1) {
        XMoveResizeWindow(dpy, workspaces[ws][0].win, 0, 0, sw, sh);
    } else {
        int mw = sw / 2;
        XMoveResizeWindow(dpy, workspaces[ws][0].win, 0, 0, mw, sh);

        int th = sh / (n - 1);
        for (int i = 1; i < n; i++) {
            XMoveResizeWindow(dpy, workspaces[ws][i].win, mw, (i - 1) * th, sw - mw, th);
        }
    }

    bar_send_update();
}

void add_client(Window w)
{
    if (win_counts[curr] >= MAX_WINDOWS || w == None || w == root)
        return;

    workspaces[curr][win_counts[curr]++].win = w;
    XSelectInput(dpy, w, EnterWindowMask | FocusChangeMask);
}

void remove_client(Window w)
{
    for (int ws = 0; ws < MAX_WORKSPACES; ws++) {
        for (int i = 0; i < win_counts[ws]; i++) {
            if (workspaces[ws][i].win == w) {
                for (int j = i; j < win_counts[ws] - 1; j++)
                    workspaces[ws][j] = workspaces[ws][j + 1];

                win_counts[ws]--;
                if (ws == curr)
                    tile(ws);
                return;
            }
        }
    }
}

void goto_workspace(int next)
{
    if (next == curr || next < 0 || next >= MAX_WORKSPACES)
        return;

    for (int i = 0; i < win_counts[curr]; i++)
        XUnmapWindow(dpy, workspaces[curr][i].win);

    curr = next;

    for (int i = 0; i < win_counts[curr]; i++)
        XMapWindow(dpy, workspaces[curr][i].win);

    tile(curr);
}

void spawn(char *const argv[])
{
    if (fork() == 0) {
        if (dpy)
            close(ConnectionNumber(dpy));
        setsid();
        execvp(argv[0], argv);
        exit(0);
    }
}

/* ---------- MAIN ---------- */

int main(int argc, char *argv[])
{
    XEvent ev;
    wm_path = argv[0];

    if (!(dpy = XOpenDisplay(NULL)))
        return 1;

    root = DefaultRootWindow(dpy);
    wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);

    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask);

    XGrabKey(dpy, KEY_RETURN, MOD, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, KEY_Q, MOD | ShiftMask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, KEY_R, MOD | ShiftMask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, KEY_D, MOD, root, True, GrabModeAsync, GrabModeAsync);
    for (int k = KEY_1; k <= KEY_9; k++)
        XGrabKey(dpy, k, MOD, root, True, GrabModeAsync, GrabModeAsync);

    bar_ipc_init();

    while (!XNextEvent(dpy, &ev)) {

        bar_try_accept();

        if (ev.type == MapRequest) {
            XWindowAttributes wa;
            XGetWindowAttributes(dpy, ev.xmaprequest.window, &wa);
            if (!wa.override_redirect) {
                add_client(ev.xmaprequest.window);
                XMapWindow(dpy, ev.xmaprequest.window);
                tile(curr);
            }
        }
        else if (ev.type == DestroyNotify || ev.type == UnmapNotify) {
            remove_client(ev.type == DestroyNotify ? ev.xdestroywindow.window : ev.xunmap.window);
        }
        else if (ev.type == KeyPress) {
            KeyCode kc = ev.xkey.keycode;
            unsigned int state = ev.xkey.state;

            if (kc == KEY_RETURN && (state & MOD))
                spawn((char*[]){"st", NULL});

            else if (kc == KEY_D && (state & MOD))
                spawn((char*[]){"dmenu_run", NULL});

            else if (kc >= KEY_1 && kc <= KEY_9 && (state & MOD))
                goto_workspace(kc - KEY_1);
        }
    }

    if (bar_client >= 0) close(bar_client);
    if (bar_server >= 0) close(bar_server);
    unlink("/tmp/shedwm_bar.sock");

    return 0;
}
