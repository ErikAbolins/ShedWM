#ifndef STATUS_H
#define STATUS_H
#define MAX_WS 9

typedef struct {
    int num;
    int occupied;
    int urgent;
} Workspace;

typedef struct {
    int focused;
    Workspace ws[MAX_WS];
} BarState;

void parse_status_json(const char *json, BarState *state);

#endif