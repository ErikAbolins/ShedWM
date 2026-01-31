#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

/*
 * Hard limits for simplicity
 */
#define MAX_WINDOWS 50
#define MAX_WORKSPACES 9
#define MOD Mod4Mask   // Super / Windows key

/*
 * Keycodes (layout-independent)
 * Use `xev` to verify these on your system
 */
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

/*
 * Client structure
 * Right now it only stores the X11 window ID
 */
typedef struct {
    Window win;
} Client;

/*
 * Workspace storage:
 *  - workspaces[ws][i] is the i-th window in workspace ws
 *  - win_counts[ws] is how many windows that workspace has
 */
Client workspaces[MAX_WORKSPACES][MAX_WINDOWS];
int win_counts[MAX_WORKSPACES] = {0};

/*
 * Global state
 */
int curr = 0;          // current workspace
Display *dpy;          // X server connection
Window root;           // root window
Atom wm_delete;        // WM_DELETE_WINDOW atom
char *wm_path;         // path to this WM binary (for restart)

/*
 * Check whether a window supports a specific WM protocol
 * (used mainly to see if WM_DELETE_WINDOW is supported)
 */
int supports_protocol(Window w, Atom proto) {
    int n;
    Atom *protos;
    int found = 0;

    if (XGetWMProtocols(dpy, w, &protos, &n)) {
        while (!found && n--)
            found = protos[n] == proto;
        XFree(protos);
    }
    return found;
}

/*
 * Arrange windows in a simple tiling layout
 *
 * Layout:
 *  - 1 window: fullscreen
 *  - >1 windows:
 *      * first window = master (left half)
 *      * remaining windows stacked vertically on the right
 */
void tile(int ws) {
    int n = win_counts[ws];
    if (n == 0) return;

    int sw = DisplayWidth(dpy, DefaultScreen(dpy));
    int sh = DisplayHeight(dpy, DefaultScreen(dpy));

    if (n == 1) {
        // Single window takes whole screen
        XMoveResizeWindow(dpy, workspaces[ws][0].win, 0, 0, sw, sh);
    } else {
        int mw = sw / 2;            // master width
        XMoveResizeWindow(dpy, workspaces[ws][0].win, 0, 0, mw, sh);

        int th = sh / (n - 1);      // tile height for stack
        for (int i = 1; i < n; i++) {
            XMoveResizeWindow(
                dpy,
                workspaces[ws][i].win,
                mw,
                (i - 1) * th,
                sw - mw,
                th
            );
        }
    }
}

/*
 * Add a new window to the current workspace
 * Ignores root window and overflows
 */
void add_client(Window w) {
    if (win_counts[curr] >= MAX_WINDOWS || w == None || w == root)
        return;

    workspaces[curr][win_counts[curr]++].win = w;

    // Track focus changes for focus-follows-mouse
    XSelectInput(dpy, w, EnterWindowMask | FocusChangeMask);
}

/*
 * Remove a window from whatever workspace it belongs to
 * Called when a window is destroyed or unmapped
 */
void remove_client(Window w) {
    for (int ws = 0; ws < MAX_WORKSPACES; ws++) {
        for (int i = 0; i < win_counts[ws]; i++) {
            if (workspaces[ws][i].win == w) {
                // Shift remaining windows left
                for (int j = i; j < win_counts[ws] - 1; j++)
                    workspaces[ws][j] = workspaces[ws][j + 1];

                win_counts[ws]--;

                // Retile if it was on the current workspace
                if (ws == curr)
                    tile(ws);
                return;
            }
        }
    }
}

/*
 * Close a client window gracefully if possible
 * Falls back to XKillClient if WM_DELETE_WINDOW isn't supported
 */
void kill_client(Window w) {
    if (w == None || w == root)
        return;

    if (supports_protocol(w, wm_delete)) {
        // Send WM_DELETE_WINDOW event
        XEvent ev = { .type = ClientMessage };
        ev.xclient.window = w;
        ev.xclient.message_type = XInternAtom(dpy, "WM_PROTOCOLS", False);
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = wm_delete;
        ev.xclient.data.l[1] = CurrentTime;
        XSendEvent(dpy, w, False, NoEventMask, &ev);
    } else {
        // Force kill
        XKillClient(dpy, w);
    }
}

/*
 * Switch to another workspace
 *  - unmaps windows from current workspace
 *  - maps windows in the target workspace
 *  - retile
 */
void goto_workspace(int next) {
    if (next == curr || next < 0 || next >= MAX_WORKSPACES)
        return;

    for (int i = 0; i < win_counts[curr]; i++)
        XUnmapWindow(dpy, workspaces[curr][i].win);

    curr = next;

    for (int i = 0; i < win_counts[curr]; i++)
        XMapWindow(dpy, workspaces[curr][i].win);

    tile(curr);
}

/*
 * Spawn a new process (terminal, dmenu, etc.)
 * Forks and detaches from the WM
 */
void spawn(char *const argv[]) {
    if (fork() == 0) {
        if (dpy)
            close(ConnectionNumber(dpy));

        setsid();           // new session
        execvp(argv[0], argv);

        // Only reached if exec fails
        fprintf(stderr, "tinywm: execvp %s failed\n", argv[0]);
        exit(0);
    }
}

/*
* AutoStart
*/
void autostart() {
    spawn((char*[]) {"i3bar", NULL});
}

/*
 * Main entry point
 *  - connects to X
 *  - grabs keys
 *  - handles X events
 */
int main(int argc, char *argv[]) {
    XEvent ev;
    wm_path = argv[0];

    if (!(dpy = XOpenDisplay(NULL)))
        return 1;

    root = DefaultRootWindow(dpy);
    wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);

    // Become the window manager
    XSelectInput(dpy, root,
        SubstructureRedirectMask | SubstructureNotifyMask);

    /*
     * Global keybindings
     */
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

    autostart();

    /*
     * Main event loop
     */
    while (!XNextEvent(dpy, &ev)) {

        if (ev.type == MapRequest) {
            // New window wants to appear
            XWindowAttributes wa;
            XGetWindowAttributes(dpy, ev.xmaprequest.window, &wa);

            if (!wa.override_redirect) {
                add_client(ev.xmaprequest.window);
                XMapWindow(dpy, ev.xmaprequest.window);
                tile(curr);
            }
        }

        else if (ev.type == DestroyNotify || ev.type == UnmapNotify) {
            // Window was closed or unmapped
            remove_client(
                ev.type == DestroyNotify ?
                ev.xdestroywindow.window :
                ev.xunmap.window
            );
        }

        else if (ev.type == EnterNotify) {
            // Focus-follows-mouse
            if (ev.xcrossing.window != root &&
                ev.xcrossing.window != None) {
                XSetInputFocus(
                    dpy,
                    ev.xcrossing.window,
                    RevertToParent,
                    CurrentTime
                );
            }
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
                // Restart WM
                XCloseDisplay(dpy);
                execvp(wm_path, argv);
            }

            else if (kc >= KEY_1 && kc <= KEY_9 && (state & MOD)) {
                goto_workspace(kc - KEY_1);
            }
        }
    }

    return 0;
}
