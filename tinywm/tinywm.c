#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define MAX_WINDOWS 50
#define MAX_WORKSPACES 9
#define MOD Mod4Mask

typedef struct {
    Window win;
} Client;

Client workspaces[MAX_WORKSPACES][MAX_WINDOWS];
int win_counts[MAX_WORKSPACES] = {0};
int curr = 0;
Display *dpy;
Window root;
Atom wm_delete;

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
        for (int i = 1; i < n; i++) {
            XMoveResizeWindow(dpy, workspaces[ws][i].win, mw, (i - 1) * th, sw - mw, th);
        }
    }
    XSync(dpy, False);
}

void add_client(Window w) {
    if (win_counts[curr] >= MAX_WINDOWS) return;
    // Don't manage the root window or random nulls
    if (w == None || w == root) return;
    
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
    XEvent ev;
    ev.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = XInternAtom(dpy, "WM_PROTOCOLS", False);
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = wm_delete;
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(dpy, w, False, NoEventMask, &ev);
}

void goto_workspace(int next) {
    if (next == curr || next < 0 || next >= MAX_WORKSPACES) return;
    for (int i = 0; i < win_counts[curr]; i++) XUnmapWindow(dpy, workspaces[curr][i].win);
    curr = next;
    for (int i = 0; i < win_counts[curr]; i++) XMapWindow(dpy, workspaces[curr][i].win);
    tile(curr);
}

int main(void) {
    XEvent ev;
    if (!(dpy = XOpenDisplay(NULL))) return 1;
    root = DefaultRootWindow(dpy);
    wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);

    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask);

    // Keybindings
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_Return), MOD, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_q), MOD | ShiftMask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_r), MOD | ShiftMask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_d), MOD, root, True, GrabModeAsync, GrabModeAsync);

    for (int i = 1; i <= 9; i++) {
        XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym((char[]){i + '0', 0})), MOD, root, True, GrabModeAsync, GrabModeAsync);
    }

    while (!XNextEvent(dpy, &ev)) {
        if (ev.type == MapRequest) {
            XWindowAttributes wa;
            XGetWindowAttributes(dpy, ev.xmaprequest.window, &wa);
            if (wa.override_redirect) continue; // Don't manage tooltips/menus

            add_client(ev.xmaprequest.window);
            XMapWindow(dpy, ev.xmaprequest.window);
            tile(curr);
        } 
        else if (ev.type == DestroyNotify || ev.type == UnmapNotify) {
            Window w = (ev.type == DestroyNotify) ? ev.xdestroywindow.window : ev.xunmap.window;
            remove_client(w);
        } 
        else if (ev.type == EnterNotify) {
            if (ev.xcrossing.window != None && ev.xcrossing.window != root) {
                XSetInputFocus(dpy, ev.xcrossing.window, RevertToParent, CurrentTime);
            }
        } 
        else if (ev.type == KeyPress) {
            KeySym keysym = XLookupKeysym(&ev.xkey, 0);
            if (keysym == XK_Return && (ev.xkey.state & MOD)) {
                if (fork() == 0) execlp("st", "st", NULL), exit(0);
            } else if (keysym == XK_q && (ev.xkey.state & (MOD | ShiftMask))) {
                Window focused; int revert;
                XGetInputFocus(dpy, &focused, &revert);
                kill_client(focused);
            } else if (keysym == XK_r && (ev.xkey.state & (MOD | ShiftMask))) {
                execvp("./tinywm", (char *[]){"./tinywm", NULL});
            } else if (keysym == XK_d && (ev.xkey.state & MOD)) {
                if (fork() == 0) execlp("dmenu_run", "dmenu_run", NULL), exit(0);
            } else if (keysym >= XK_1 && keysym <= XK_9) {
                goto_workspace(keysym - XK_1);
            }
        }
    }
    return 0;
}