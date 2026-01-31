#include "status.h"
#include <cjson/cJSON.h>
#include <string.h>

void parse_status_json(const char *json, BarState *state)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return;

    state->focused = cJSON_GetObjectItem(root, "focused")->valueint;

    cJSON *arr = cJSON_GetObjectItem(root, "workspaces");

    for (int i = 0; i < MAX_WS; i++) {
        cJSON *ws = cJSON_GetArrayItem(arr, i);
        state->ws[i].num = cJSON_GetObjectItem(ws, "num")->valueint;
        state->ws[i].occupied = cJSON_IsTrue(cJSON_GetObjectItem(ws, "occupied"));
        state->ws[i].urgent = cJSON_IsTrue(cJSON_GetObjectItem(ws, "urgent"));
    }

    cJSON_Delete(root);
}
