/*
 * This software is in the public domain
 * and is provided AS IS, with NO WARRANTY.
 */

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MOD Mod4Mask
#define MAX_WINDOWS 50
#define MAX_WORKSPACES 9

typedef struct {
    Window win;
    int x, y, w, h;
} Client;

Client workspaces[MAX_WORKSPACES][MAX_WINDOWS];
int workspace_counts[MAX_WORKSPACES] = {0};
int current_workspace = 0;

#define clients (workspaces[current_workspace])
#define nclients (workspace_counts[current_workspace])

void add_client(Window win) {
    if (nclients >= MAX_WINDOWS) return;
    clients[nclients].win = win;
    nclients++;
    fprintf(stderr, "Added client %lu to workspace %d\n", win, current_workspace);
}

void remove_client(Window win){
    for(int i = 0; i < nclients; i++){
        if(clients[i].win == win) {
            for(int j = i; j < nclients - 1; j++){
                clients[j] = clients[j + 1];
            }
            nclients--;
            fprintf(stderr, "Removed client %lu from workspace %d\n", win, current_workspace);
            return;
        }
    }
}

void tile(Display *dpy, int screen_w, int screen_h){
    if(nclients == 0) return;

    if(nclients == 1) {
        XMoveResizeWindow(dpy, clients[0].win, 0, 0, screen_w, screen_h);
    } else {
        int master_w = screen_w / 2;
        int stack_w = screen_w - master_w;
        int stack_h = screen_h / (nclients - 1);

        XMoveResizeWindow(dpy, clients[0].win, 0, 0, master_w, screen_h);

        for(int i = 1; i < nclients; i++) {
            XMoveResizeWindow(dpy, clients[i].win,
                master_w, (i - 1) * stack_h,
                stack_w, stack_h);
        }
    }
}

int main(void)
{
    Display *dpy;
    XWindowAttributes attr;
    XButtonEvent start;
    XEvent ev;
    Window focused = None;

    if(!(dpy = XOpenDisplay(NULL))) return 1;
    XSelectInput(dpy, DefaultRootWindow(dpy),
        SubstructureRedirectMask | SubstructureNotifyMask);

    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_Return), MOD,
        DefaultRootWindow(dpy), True, GrabModeAsync, GrabModeAsync);

    XGrabKey(dpy, 24, MOD | ShiftMask,
        DefaultRootWindow(dpy), True, GrabModeAsync, GrabModeAsync);

    XGrabKey(dpy, 27, MOD | ShiftMask,
        DefaultRootWindow(dpy), True, GrabModeAsync, GrabModeAsync);

    for(int i = 1; i <= 9; i++) {
        char numkey[2];
        sprintf(numkey, "%d", i);
        XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym(numkey)), MOD,
            DefaultRootWindow(dpy), True, GrabModeAsync, GrabModeAsync);
    }

    XGrabKey(dpy, 40, MOD, DefaultRootWindow(dpy),
        True, GrabModeAsync, GrabModeAsync);

    start.subwindow = None;

    for(;;) {
        XNextEvent(dpy, &ev);

        if(ev.type == KeyPress) {
            if(ev.xkey.state == MOD &&
               ev.xkey.keycode == XKeysymToKeycode(dpy, XK_Return)) {
                if(fork() == 0) {
                    execl("/usr/local/bin/st", "st", NULL);
                    exit(0);
                }
            }

            else if(ev.xkey.state == (MOD | ShiftMask) && ev.xkey.keycode == 24) {
                if(focused != None) {
                    XDestroyWindow(dpy, focused);
                    XFlush(dpy);
                    remove_client(focused);
                    focused = None;
                    
                    Screen *s = DefaultScreenOfDisplay(dpy);
                    tile(dpy, s->width, s->height);
                }
            }

            else if(ev.xkey.state == (MOD | ShiftMask) && ev.xkey.keycode == 27) {
                execvp("./tinywm", (char *[]){"./tinywm", NULL});
            }

            else if(ev.xkey.state == MOD && ev.xkey.keycode == 40) {
                if(fork() == 0) {
                    execl("/usr/bin/dmenu_run", "dmenu_run", NULL);
                    exit(0);
                }
            }

            for(int i = 1; i <= 9; i++) {
                char numkey[2];
                sprintf(numkey, "%d", i);

                if(ev.xkey.keycode ==
                   XKeysymToKeycode(dpy, XStringToKeysym(numkey))) {

                    // Save old workspace
                    int old_workspace = current_workspace;

                    // Hide OLD workspace windows
                    for(int j = 0; j < workspace_counts[old_workspace]; j++)
                        XUnmapWindow(dpy, workspaces[old_workspace][j].win);

                    // Switch to new workspace
                    current_workspace = i - 1;
                    focused = None;  // Clear focus when switching workspaces

                    fprintf(stderr, "Switched to workspace %d\n", current_workspace);

                    // Show NEW workspace windows
                    for(int j = 0; j < workspace_counts[current_workspace]; j++)
                        XMapWindow(dpy, workspaces[current_workspace][j].win);

                    Screen *s = DefaultScreenOfDisplay(dpy);
                    tile(dpy, s->width, s->height);
                    break;  // Don't check other numbers
                }
            }
        }

        else if(ev.type == MapRequest) {
            XMapWindow(dpy, ev.xmaprequest.window);
            add_client(ev.xmaprequest.window);

            Screen *s = DefaultScreenOfDisplay(dpy);
            tile(dpy, s->width, s->height);
        }

        else if(ev.type == DestroyNotify) {
            remove_client(ev.xdestroywindow.window);

            Screen *s = DefaultScreenOfDisplay(dpy);
            tile(dpy, s->width, s->height);
        }

        else if(ev.type == ButtonPress && ev.xbutton.subwindow != None) {
            focused = ev.xbutton.subwindow;
            XGetWindowAttributes(dpy, focused, &attr);
            start = ev.xbutton;
        }

        else if(ev.type == MotionNotify && start.subwindow != None) {
            int xdiff = ev.xbutton.x_root - start.x_root;
            int ydiff = ev.xbutton.y_root - start.y_root;
            XMoveResizeWindow(dpy, start.subwindow,
                attr.x + (start.button==1 ? xdiff : 0),
                attr.y + (start.button==1 ? ydiff : 0),
                MAX(1, attr.width  + (start.button==3 ? xdiff : 0)),
                MAX(1, attr.height + (start.button==3 ? ydiff : 0)));
        }

        else if(ev.type == ButtonRelease)
            start.subwindow = None;
    }
}