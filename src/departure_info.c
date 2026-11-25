#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "departure_info.h"
#include "schema.h"

static void copy_str(char *dst, size_t dst_sz, const cJSON *obj, const char *key) {
    const char *s = cJSON_GetStringValue(cJSON_GetObjectItem(obj, key));
    snprintf(dst, dst_sz, "%s", s ? s : "");
}

DepartureInfo parse_departure_info(const cJSON *dep, int delay_threshold_min) {
    DepartureInfo info = {0};

    copy_str(info.planned_time, sizeof(info.planned_time), dep, DEP_PLANNED_TIME);
    copy_str(info.changed_time, sizeof(info.changed_time), dep, DEP_CHANGED_TIME);
    info.planned_ts = cJSON_GetNumberValue(cJSON_GetObjectItem(dep, DEP_PLANNED_TS));
    info.changed_ts = cJSON_GetNumberValue(cJSON_GetObjectItem(dep, DEP_CHANGED_TS));
    copy_str(info.train, sizeof(info.train), dep, DEP_TRAIN);
    copy_str(info.destination, sizeof(info.destination), dep, DEP_DESTINATION);
    copy_str(info.planned_platform, sizeof(info.planned_platform), dep, DEP_PLANNED_PLATFORM);
    copy_str(info.changed_platform, sizeof(info.changed_platform), dep, DEP_CHANGED_PLATFORM);

    const char *stat = cJSON_GetStringValue(cJSON_GetObjectItem(dep, DEP_STATUS));
    info.status = (stat && stat[0]) ? stat[0] : 'p';
    info.delay_min = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(dep, DEP_DELAY_MIN));
    copy_str(info.mark, sizeof(info.mark), dep, DEP_MARK);
    copy_str(info.mark_bg, sizeof(info.mark_bg), dep, DEP_MARK_BG);

    departure_info_derive(&info, delay_threshold_min);
    return info;
}

void departure_info_derive(DepartureInfo *info, int delay_threshold_min) {
    info->cancelled = (info->status == 'c');
    info->show_delay = !info->cancelled && info->changed_ts > 0 && info->delay_min != 0 &&
                       abs(info->delay_min) >= delay_threshold_min;
    info->effective_ts = (info->show_delay || !(info->planned_ts > 0))
                       ? info->changed_ts : info->planned_ts;
}

int departure_info_cmp(const void *a, const void *b) {
    const DepartureInfo *x = a, *y = b;
    if (x->effective_ts != y->effective_ts) return x->effective_ts < y->effective_ts ? -1 : 1;
    if (x->planned_ts != y->planned_ts)     return x->planned_ts < y->planned_ts ? -1 : 1;
    return strcmp(x->destination, y->destination);
}

bool departure_mark_match(const char *spec, const char *path,
                          char *emoji, size_t emoji_sz, char *bg, size_t bg_sz) {
    if (emoji_sz) emoji[0] = '\0';
    if (bg_sz)    bg[0] = '\0';
    if (!spec || !path || !*path) return false;
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", spec);
    for (char *save = NULL, *e = strtok_r(buf, ",", &save); e; e = strtok_r(NULL, ",", &save)) {
        while (*e == ' ') e++;
        char *f[3] = { e, NULL, NULL };
        for (int i = 1; i < 3; i++) {
            char *c = f[i - 1] ? strchr(f[i - 1], ':') : NULL;
            if (c) { *c = '\0'; f[i] = c + 1; }
        }
        for (int i = 0; i < 3; i++)       /* trim trailing spaces */
            for (size_t n = f[i] ? strlen(f[i]) : 0; n && f[i][n - 1] == ' '; n--) f[i][n - 1] = '\0';
        if (!f[0][0] || !f[1] || !f[1][0]) continue;
        if (!strstr(path, f[0])) continue;
        snprintf(emoji, emoji_sz, "%s", f[1]);
        if (f[2] && f[2][0] == '#' && strlen(f[2]) == 7) snprintf(bg, bg_sz, "%s", f[2]);
        return true;
    }
    return false;
}

bool departure_info_departed(const DepartureInfo *info, time_t now, int grace_s) {
    return info->effective_ts > 0 && info->effective_ts + grace_s < (double)now;
}

cJSON *serialize_departure_info(const DepartureInfo *info) {
    cJSON *dep = cJSON_CreateObject();
    cJSON_AddStringToObject(dep, DEP_PLANNED_TIME,     info->planned_time);
    cJSON_AddStringToObject(dep, DEP_CHANGED_TIME,     info->changed_time);
    cJSON_AddNumberToObject(dep, DEP_PLANNED_TS,       info->planned_ts);
    cJSON_AddNumberToObject(dep, DEP_CHANGED_TS,       info->changed_ts);
    cJSON_AddStringToObject(dep, DEP_TRAIN,            info->train);
    cJSON_AddStringToObject(dep, DEP_DESTINATION,      info->destination);
    cJSON_AddStringToObject(dep, DEP_PLANNED_PLATFORM, info->planned_platform);
    cJSON_AddStringToObject(dep, DEP_CHANGED_PLATFORM, info->changed_platform);
    char stat[2] = { info->status, '\0' };
    cJSON_AddStringToObject(dep, DEP_STATUS,           stat);
    cJSON_AddNumberToObject(dep, DEP_DELAY_MIN,        info->delay_min);
    if (info->mark[0]) {
        cJSON_AddStringToObject(dep, DEP_MARK,    info->mark);
        cJSON_AddStringToObject(dep, DEP_MARK_BG, info->mark_bg);
    }
    return dep;
}
