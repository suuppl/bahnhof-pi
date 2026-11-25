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
    return dep;
}
