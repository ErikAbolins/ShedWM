#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>

#define MAX_WORKSPACES 9
#define MOD Mod4Mask

#define KEY_RETURN 36
#define KEY_Q 24
#define KEY_R 27
#define KEY_D 40
#define KEY_1 10
#define KEY_9 18

/* ---------- FORWARD DECLARATIONS ---------- */
void bar_send_update();

Window focused_win = None;

/* ---------- BSP STRUCTURES ---------- */

typedef struct {
    int x, y;
    int width, height;
} Rect;

typedef enum {
    SPLIT_VERTICAL,
    SPLIT_HORIZONTAL
} SplitType;

typedef struct BSPNode {
    int is_leaf;
    Window win;
    
    SplitType split;
    float ratio;
    
    struct BSPNode *left;
    struct BSPNode *right;
    struct BSPNode *parent;
} BSPNode;

/* ---------- GLOBALS ---------- */

BSPNode *workspace_trees[MAX_WORKSPACES] = {NULL};
int curr = 0;

Display *dpy;
Window root;
Atom wm_delete;
char *wm_path;

int bar_server = -1;
int bar_client = -1;

/* ---------- BSP FUNCTIONS ---------- */

BSPNode* create_leaf(Window w) {
    fprintf(stderr, "create_leaf: Creating leaf for window %lu\n", w);
    BSPNode *node = calloc(1, sizeof(BSPNode));
    if (!node) {
        fprintf(stderr, "ERROR: calloc failed in create_leaf\n");
        return NULL;
    }
    node->is_leaf = 1;
    node->win = w;
    node->ratio = 0.5;
    node->left = NULL;
    node->right = NULL;
    node->parent = NULL;
    fprintf(stderr, "create_leaf: Created node at %p\n", (void*)node);
    return node;
}

BSPNode* create_container(SplitType split) {
    fprintf(stderr, "create_container: Creating container with split %d\n", split);
    BSPNode *node = calloc(1, sizeof(BSPNode));
    if (!node) {
        fprintf(stderr, "ERROR: calloc failed in create_container\n");
        return NULL;
    }
    node->is_leaf = 0;
    node->split = split;
    node->ratio = 0.5;
    node->win = None;
    node->left = NULL;
    node->right = NULL;
    node->parent = NULL;
    return node;
}

void tile_recursive(Display *dpy, BSPNode *node, Rect rect) {
    if (!node) {
        fprintf(stderr, "tile_recursive: node is NULL\n");
        return;
    }
    
    if (node->is_leaf) {
        fprintf(stderr, "tile_recursive: Tiling leaf window %lu at (%d,%d) %dx%d\n", 
                node->win, rect.x, rect.y, rect.width, rect.height);
        XMoveResizeWindow(dpy, node->win, rect.x, rect.y, rect.width, rect.height);
        return;
    }
    
    fprintf(stderr, "tile_recursive: Container node, split=%d, ratio=%.2f\n", 
            node->split, node->ratio);
    
    Rect left_rect, right_rect;
    
    if (node->split == SPLIT_VERTICAL) {
        int split_x = rect.x + (int)(rect.width * node->ratio);
        left_rect = (Rect){rect.x, rect.y, split_x - rect.x, rect.height};
        right_rect = (Rect){split_x, rect.y, rect.width - (split_x - rect.x), rect.height};
        fprintf(stderr, "  VERT split at x=%d\n", split_x);
    } else {
        int split_y = rect.y + (int)(rect.height * node->ratio);
        left_rect = (Rect){rect.x, rect.y, rect.width, split_y - rect.y};
        right_rect = (Rect){rect.x, split_y, rect.width, rect.height - (split_y - rect.y)};
        fprintf(stderr, "  HORIZ split at y=%d\n", split_y);
    }
    
    tile_recursive(dpy, node->left, left_rect);
    tile_recursive(dpy, node->right, right_rect);
}

BSPNode* find_node(BSPNode *root, Window w) {
    if (!root) return NULL;
    if (root->is_leaf && root->win == w) return root;
    
    BSPNode *found = find_node(root->left, w);
    if (found) return found;
    return find_node(root->right, w);
}

int count_leaves(BSPNode *node) {
    if (!node) return 0;
    if (node->is_leaf) return 1;
    return count_leaves(node->left) + count_leaves(node->right);
}

BSPNode* get_any_leaf(BSPNode *node) {
    if (!node) return NULL;
    if (node->is_leaf) return node;
    BSPNode *leaf = get_any_leaf(node->left);
    return leaf ? leaf : get_any_leaf(node->right);
}

void insert_window(BSPNode **root, Window w) {
    fprintf(stderr, "insert_window: root=%p, w=%lu\n", (void*)*root, w);
    
    if (!*root) {
        fprintf(stderr, "insert_window: Creating first leaf node\n");
        *root = create_leaf(w);
        fprintf(stderr, "insert_window: First node created at %p\n", (void*)*root);
        return;
    }
    
    fprintf(stderr, "insert_window: Finding target to split\n");
    BSPNode *target = get_any_leaf(*root);
    if (!target) {
        fprintf(stderr, "ERROR: get_any_leaf returned NULL\n");
        return;
    }
    if (!target->is_leaf) {
        fprintf(stderr, "ERROR: target is not a leaf\n");
        return;
    }
    
    fprintf(stderr, "insert_window: Target found at %p, win=%lu\n", (void*)target, target->win);
    
    // Alternate split direction based on current count
    int leaf_count = count_leaves(*root);
    SplitType split = (leaf_count % 2 == 0) ? SPLIT_VERTICAL : SPLIT_HORIZONTAL;
    fprintf(stderr, "insert_window: Using split type %d (leaf_count=%d)\n", split, leaf_count);
    
    BSPNode *old_win = create_leaf(target->win);
    BSPNode *new_win = create_leaf(w);
    
    if (!old_win || !new_win) {
        fprintf(stderr, "ERROR: Failed to create child nodes\n");
        return;
    }
    
    fprintf(stderr, "insert_window: Converting target to container\n");
    target->is_leaf = 0;
    target->split = split;
    target->ratio = 0.5;
    target->left = old_win;
    target->right = new_win;
    target->win = None;
    
    old_win->parent = target;
    new_win->parent = target;
    
    fprintf(stderr, "insert_window: Done. Tree now has %d leaves\n", count_leaves(*root));
}

void remove_window(BSPNode **root, Window w) {
    fprintf(stderr, "remove_window: Looking for window %lu\n", w);
    BSPNode *node = find_node(*root, w);
    if (!node) {
        fprintf(stderr, "remove_window: Window not found in tree\n");
        return;
    }
    
    fprintf(stderr, "remove_window: Found node at %p\n", (void*)node);
    
    if (!node->parent) {
        fprintf(stderr, "remove_window: Removing root node\n");
        free(*root);
        *root = NULL;
        return;
    }
    
    BSPNode *parent = node->parent;
    BSPNode *sibling = (parent->left == node) ? parent->right : parent->left;
    
    fprintf(stderr, "remove_window: Collapsing parent, promoting sibling\n");
    
    if (parent->parent) {
        if (parent->parent->left == parent)
            parent->parent->left = sibling;
        else
            parent->parent->right = sibling;
        sibling->parent = parent->parent;
    } else {
        *root = sibling;
        sibling->parent = NULL;
    }
    
    free(node);
    free(parent);
    fprintf(stderr, "remove_window: Done\n");
}

void tile_workspace(int ws) {
    fprintf(stderr, "tile_workspace: ws=%d\n", ws);
    if (!workspace_trees[ws]) {
        fprintf(stderr, "tile_workspace: workspace_trees[%d] is NULL, nothing to tile\n", ws);
        return;
    }
    
    int sw = DisplayWidth(dpy, DefaultScreen(dpy));
    int sh = DisplayHeight(dpy, DefaultScreen(dpy));
    
    fprintf(stderr, "tile_workspace: Screen size %dx%d\n", sw, sh);
    Rect screen = {0, 0, sw, sh};
    tile_recursive(dpy, workspace_trees[ws], screen);
    fprintf(stderr, "tile_workspace: Done\n");
    
    bar_send_update();
}

/* ---------- BAR IPC ---------- */

void bar_ipc_init() {
    fprintf(stderr, "bar_ipc_init: Starting\n");
    struct sockaddr_un addr = {0};
    
    bar_server = socket(AF_UNIX, SOCK_STREAM, 0);
    if (bar_server < 0) {
        fprintf(stderr, "bar_ipc_init: Failed to create socket\n");
        return;
    }
    
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, "/tmp/shedwm_bar.sock");
    
    unlink(addr.sun_path);
    if (bind(bar_server, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "bar_ipc_init: Failed to bind socket\n");
        return;
    }
    listen(bar_server, 1);
    
    fcntl(bar_server, F_SETFL, O_NONBLOCK);
    fprintf(stderr, "bar_ipc_init: Socket created successfully\n");
}

void bar_try_accept() {
    if (bar_client >= 0) return;
    bar_client = accept(bar_server, NULL, NULL);
    if (bar_client >= 0) {
        fprintf(stderr, "bar_try_accept: Bar connected\n");
    }
}

void bar_send_update() {
    if (bar_client < 0) return;
    
    char json[512];
    int len = 0;
    
    len += sprintf(json + len, "{ \"focused\": %d, \"workspaces\": [", curr + 1);
    
    for (int i = 0; i < MAX_WORKSPACES; i++) {
        int occupied = workspace_trees[i] != NULL;
        len += sprintf(json + len,
            "{\"num\":%d,\"occupied\":%s}%s",
            i + 1,
            occupied ? "true" : "false",
            (i < MAX_WORKSPACES - 1) ? "," : ""
        );
    }
    
    len += sprintf(json + len, "] }\n");
    
    if (write(bar_client, json, len) <= 0) {
        fprintf(stderr, "bar_send_update: Bar disconnected\n");
        close(bar_client);
        bar_client = -1;
    }
}

/* ---------- WINDOW MANAGEMENT ---------- */

int supports_protocol(Window w, Atom proto) {
    Atom *protos;
    int n, found = 0;
    
    if (XGetWMProtocols(dpy, w, &protos, &n)) {
        while (!found && n--)
            found = protos[n] == proto;
        XFree(protos);
    }
    return found;
}

void add_client(Window w) {
    fprintf(stderr, "add_client: w=%lu\n", w);
    if (w == None || w == root) {
        fprintf(stderr, "add_client: Skipping (None or root)\n");
        return;
    }
    
    fprintf(stderr, "add_client: Adding to workspace %d\n", curr);
    insert_window(&workspace_trees[curr], w);
    XSelectInput(dpy, w, EnterWindowMask | FocusChangeMask);
    fprintf(stderr, "add_client: Done\n");
}

void remove_client(Window w) {
    fprintf(stderr, "remove_client: w=%lu\n", w);
    for (int ws = 0; ws < MAX_WORKSPACES; ws++) {
        if (find_node(workspace_trees[ws], w)) {
            fprintf(stderr, "remove_client: Found in workspace %d\n", ws);
            remove_window(&workspace_trees[ws], w);
            if (ws == curr) tile_workspace(ws);
            return;
        }
    }
    fprintf(stderr, "remove_client: Window not found in any workspace\n");
}

void unmap_tree(BSPNode *node) {
    if (!node) return;
    if (node->is_leaf) {
        XUnmapWindow(dpy, node->win);
        return;
    }
    unmap_tree(node->left);
    unmap_tree(node->right);
}

void map_tree(BSPNode *node) {
    if (!node) return;
    if (node->is_leaf) {
        XMapWindow(dpy, node->win);
        return;
    }
    map_tree(node->left);
    map_tree(node->right);
}

void goto_workspace(int next) {
    fprintf(stderr, "goto_workspace: %d -> %d\n", curr, next);
    if (next == curr || next < 0 || next >= MAX_WORKSPACES) return;
    
    unmap_tree(workspace_trees[curr]);
    curr = next;
    map_tree(workspace_trees[curr]);
    
    tile_workspace(curr);
}

void spawn(char *const argv[]) {
    fprintf(stderr, "spawn: %s\n", argv[0]);
    if (fork() == 0) {
        if (dpy) close(ConnectionNumber(dpy));
        setsid();
        execvp(argv[0], argv);
        fprintf(stderr, "spawn: execvp failed for %s\n", argv[0]);
        exit(0);
    }
}

void refreshWm(void) {
    fprintf(stderr, "refreshWm: Restarting WM\n");
    char *argv[] = {"shedwm", NULL};
    XCloseDisplay(dpy);
    execvp(argv[0], argv);
    perror("Shedwm refresh failed");
}

/* ---------- MAIN ---------- */

int main(int argc, char *argv[]) {
    fprintf(stderr, "=== SHEDWM STARTING ===\n");
    XEvent ev;
    wm_path = argv[0];
    
    if (!(dpy = XOpenDisplay(NULL))) {
        fprintf(stderr, "FATAL: Failed to open display\n");
        return 1;
    }
    fprintf(stderr, "Display opened successfully\n");
    
    root = DefaultRootWindow(dpy);
    fprintf(stderr, "Root window: %lu\n", root);
    wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    
    // EWMH hints
    fprintf(stderr, "Setting EWMH hints\n");
    Atom net_supported = XInternAtom(dpy, "_NET_SUPPORTED", False);
    Atom net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", False);
    Atom net_supporting_wm_check = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);

    Window check_win = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
    XChangeProperty(dpy, check_win, net_supporting_wm_check, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&check_win, 1);
    XChangeProperty(dpy, root, net_supporting_wm_check, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&check_win, 1);
    
    char *wm_name = "shedwm";
    XChangeProperty(dpy, check_win, net_wm_name, XInternAtom(dpy, "UTF8_STRING", False), 8, PropModeReplace, (unsigned char *)wm_name, strlen(wm_name));
    
    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask);
    fprintf(stderr, "Registered as window manager\n");
    
    fprintf(stderr, "Grabbing keys\n");
    XGrabKey(dpy, KEY_RETURN, MOD, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, KEY_Q, MOD | ShiftMask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, KEY_R, MOD | ShiftMask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, KEY_D, MOD, root, True, GrabModeAsync, GrabModeAsync);
    for (int k = KEY_1; k <= KEY_9; k++)
        XGrabKey(dpy, k, MOD, root, True, GrabModeAsync, GrabModeAsync);
    
    bar_ipc_init();
    
    fprintf(stderr, "Entering event loop\n");
    while (!XNextEvent(dpy, &ev)) {
        bar_try_accept();
        
        if (ev.type == MapRequest) {
            Window w = ev.xmaprequest.window;
            fprintf(stderr, "MapRequest: window %lu\n", w);
            
            Atom actual_type;
            int actual_format;
            unsigned long nitems, bytes_after;
            unsigned char *prop = NULL;
            
            Atom net_wm_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
            if (XGetWindowProperty(dpy, w, net_wm_type, 0, 1024, False, XA_ATOM,
                                   &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success) {
                if (prop) {
                    Atom dock = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
                    if (((Atom *)prop)[0] == dock) {
                        fprintf(stderr, "MapRequest: Is a dock, mapping without tiling\n");
                        XMapWindow(dpy, w);
                        XFree(prop);
                        continue;
                    }
                    XFree(prop);
                }
            }
            
            XWindowAttributes wa;
            XGetWindowAttributes(dpy, w, &wa);
            
            if (!wa.override_redirect) {
                fprintf(stderr, "MapRequest: Adding as managed client\n");
                add_client(w);
                XMapWindow(dpy, w);
                tile_workspace(curr);
            } else {
                fprintf(stderr, "MapRequest: override_redirect=true, not managing\n");
            }
        }
        else if (ev.type == DestroyNotify) {
            fprintf(stderr, "DestroyNotify: window %lu\n", ev.xdestroywindow.window);
            if (ev.xdestroywindow.window == focused_win) focused_win = None;
            remove_client(ev.xdestroywindow.window);
        }
        else if (ev.type == UnmapNotify) {
            fprintf(stderr, "UnmapNotify: window %lu\n", ev.xunmap.window);
            if (ev.xunmap.window == focused_win) focused_win = None;
            remove_client(ev.xunmap.window);
        }
        else if (ev.type == EnterNotify) {
            if (ev.xcrossing.window != root && ev.xcrossing.window != None) {
                focused_win = ev.xcrossing.window;
                XSetInputFocus(dpy, ev.xcrossing.window, RevertToParent, CurrentTime);
            }
                
        }
        else if (ev.type == KeyPress) {
            KeyCode kc = ev.xkey.keycode;
            unsigned int state = ev.xkey.state;
            
            if (kc == KEY_RETURN && (state & MOD)) {
                fprintf(stderr, "KeyPress: Spawning terminal\n");
                spawn((char*[]){"st", NULL});
            }
            else if (kc == KEY_D && (state & MOD)) {
                fprintf(stderr, "KeyPress: Spawning dmenu\n");
                spawn((char*[]){"dmenu_run", NULL});
            }
            else if (kc >= KEY_1 && kc <= KEY_9 && (state & MOD)) {
                fprintf(stderr, "KeyPress: Switching workspace\n");
                goto_workspace(kc - KEY_1);
            }
            else if (kc == KEY_R && (state & (MOD | ShiftMask)) == (MOD | ShiftMask)) {
                fprintf(stderr, "KeyPress: Refreshing WM\n");
                refreshWm();
            }
            else if (kc == KEY_Q && (state & (MOD | ShiftMask)) == (MOD | ShiftMask)) {
                fprintf(stderr, "KeyPress: Kill window (focused_win=%lu)\n", focused_win);
                if (focused_win != None && focused_win != root) {
                    if (supports_protocol(focused_win, wm_delete)) {
                        fprintf(stderr, "  Sending WM_DELETE_WINDOW\n");
                        XEvent msg = {.type = ClientMessage};
                        msg.xclient.window = focused_win;
                        msg.xclient.message_type = XInternAtom(dpy, "WM_PROTOCOLS", False);
                        msg.xclient.format = 32;
                        msg.xclient.data.l[0] = wm_delete;
                        msg.xclient.data.l[1] = CurrentTime;
                        XSendEvent(dpy, focused_win, False, NoEventMask, &msg);
                    } else {
                        fprintf(stderr, "  Using XKillClient\n");
                        XKillClient(dpy, focused_win);
                    }
                    XFlush(dpy);
                } else {
                    fprintf(stderr, "  No valid focused window\n");
                }
            }
        }
    }
    
    fprintf(stderr, "Event loop exited\n");
    
    if (bar_client >= 0) close(bar_client);
    if (bar_server >= 0) close(bar_server);
    unlink("/tmp/shedwm_bar.sock");
    
    return 0;
}