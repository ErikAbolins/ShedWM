#include "status.h"
#include <cjson/cJSON.h>
#include <string.h>

void parse_status_json(const char *json, BarState *state)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return;

    cJSON *focused_item = cJSON_GetObjectItem(root, "focused");
    if (!focused_item) { cJSON_Delete(root); return; }
    state->focused = focused_item->valueint;

    cJSON *arr = cJSON_GetObjectItem(root, "workspaces");
    if (!arr) { cJSON_Delete(root); return; }

    for (int i = 0; i < MAX_WS; i++) {
        cJSON *ws = cJSON_GetArrayItem(arr, i);
        if (!ws) continue;
        
        cJSON *num = cJSON_GetObjectItem(ws, "num");
        cJSON *occ = cJSON_GetObjectItem(ws, "occupied");
        cJSON *urg = cJSON_GetObjectItem(ws, "urgent");
        
        if (num) state->ws[i].num = num->valueint;
        if (occ) state->ws[i].occupied = cJSON_IsTrue(occ);
        if (urg) state->ws[i].urgent = cJSON_IsTrue(urg);
    }

    cJSON_Delete(root);
}