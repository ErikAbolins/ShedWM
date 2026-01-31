#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define MAX_WINDOWS 50
#define MAX_WORKSPACES 9
#define MOD Mod4Mask

// Keycodes - layout independent (run xev to verify yours match)
#define KEY_RETURN 36
#define KEY_Q 24
#define KEY_R 27
#define KEY_D 40
#define KEY_1 10
#define KEY_2 11
#define KEY_3 12
#define KEY_4 13
#define KEY_5 14
#define KEY_6 15
#define KEY_7 16
#define KEY_8 17
#define KEY_9 18

typedef struct { Window win; } Client;
Client workspaces[MAX_WORKSPACES][MAX_WINDOWS];
int win_counts[MAX_WORKSPACES] = {0};
int curr = 0;
Display *dpy;
Window root;
Atom wm_delete;
char *wm_path;

int supports_protocol(Window w, Atom proto) {
    int n;
    Atom *protos;
    int found = 0;
    if (XGetWMProtocols(dpy, w, &protos, &n)) {
        while (!found && n--) found = protos[n] == proto;
        XFree(protos);
    }
    return found;
}


void tile(int ws) {
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
        for (int i = 1; i < n; i++)
            XMoveResizeWindow(dpy, workspaces[ws][i].win, mw, (i - 1) * th, sw - mw, th);
    }
}

void add_client(Window w) {
    if (win_counts[curr] >= MAX_WINDOWS || w == None || w == root) return;
    workspaces[curr][win_counts[curr]++].win = w;
    XSelectInput(dpy, w, EnterWindowMask | FocusChangeMask);
}

void remove_client(Window w) {
    for (int ws = 0; ws < MAX_WORKSPACES; ws++) {
        for (int i = 0; i < win_counts[ws]; i++) {
            if (workspaces[ws][i].win == w) {
                for (int j = i; j < win_counts[ws] - 1; j++) workspaces[ws][j] = workspaces[ws][j + 1];
                win_counts[ws]--;
                if (ws == curr) tile(ws);
                return;
            }
        }
    }
}

void kill_client(Window w) {
    if (w == None || w == root) return;

    if(supports_protocol(w, wm_delete)) {
        XEvent ev = { .type = ClientMessage };
        ev.xclient.window = w;
        ev.xclient.message_type = XInternAtom(dpy, "WM_PROTOCOLS", False);
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = wm_delete;
        ev.xclient.data.l[1] = CurrentTime;
        XSendEvent(dpy, w, False, NoEventMask, &ev);
    } else {
        XKillClient(dpy, w);
    }
}

void goto_workspace(int next) {
    if (next == curr || next < 0 || next >= MAX_WORKSPACES) return;
    for (int i = 0; i < win_counts[curr]; i++) XUnmapWindow(dpy, workspaces[curr][i].win);
    curr = next;
    for (int i = 0; i < win_counts[curr]; i++) XMapWindow(dpy, workspaces[curr][i].win);
    tile(curr);
}

void spawn(char *const argv[]) {
    if (fork() == 0) {
        if (dpy) close(ConnectionNumber(dpy));
        setsid();
        execvp(argv[0], argv);
        fprintf(stderr, "tinywm: execvp %s failed\n", argv[0]);
        exit(0);
    }
}


int main(int argc, char *argv[]) {
    XEvent ev;
    wm_path = argv[0];
    if (!(dpy = XOpenDisplay(NULL))) return 1;
    root = DefaultRootWindow(dpy);
    wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask);

    // Grab keys by keycode (layout independent)
    XGrabKey(dpy, KEY_RETURN, MOD, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, KEY_Q, MOD | ShiftMask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, KEY_R, MOD | ShiftMask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, KEY_D, MOD, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, KEY_1, MOD, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, KEY_2, MOD, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, KEY_3, MOD, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, KEY_4, MOD, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, KEY_5, MOD, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, KEY_6, MOD, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, KEY_7, MOD, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, KEY_8, MOD, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, KEY_9, MOD, root, True, GrabModeAsync, GrabModeAsync);

    while (!XNextEvent(dpy, &ev)) {
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
        else if (ev.type == EnterNotify) {
            if (ev.xcrossing.window != root && ev.xcrossing.window != None) 
                XSetInputFocus(dpy, ev.xcrossing.window, RevertToParent, CurrentTime);
        } 
        else if (ev.type == KeyPress) {
            KeyCode kc = ev.xkey.keycode;
            unsigned int state = ev.xkey.state;
            
            if (kc == KEY_RETURN && (state & MOD)) {
                spawn((char*[]){"st", NULL});
            }
            else if (kc == KEY_D && (state & MOD)) {
                spawn((char*[]){"dmenu_run", NULL});
            }
            else if (kc == KEY_Q && (state & MOD) && (state & ShiftMask)) {
                Window f; int r;
                XGetInputFocus(dpy, &f, &r);
                kill_client(f);
            }
            else if (kc == KEY_R && (state & MOD) && (state & ShiftMask)) {
                XCloseDisplay(dpy);
                execvp(wm_path, argv);
            }
            else if (kc >= KEY_1 && kc <= KEY_9 && (state & MOD)) {
                goto_workspace(kc - KEY_1);
            }
            else if (kc == KEY_Q && (state & MOD) && (state & ShiftMask)) {
                Window f; int r;
                XGetInputFocus(dpy, &f, &r);
                if (f != None && f != root && f != PointerRoot) {
                    kill_client(f);
                }
            }
        }
    }
    return 0;
}
