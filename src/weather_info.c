#include <stdio.h>
#include "weather_info.h"
#include "schema.h"

WeatherInfo parse_weather_info(const cJSON *root) {
    WeatherInfo info = {0};

    info.temp            = cJSON_GetNumberValue(cJSON_GetObjectItem(root, WEA_TEMP));
    info.tempmin          = cJSON_GetNumberValue(cJSON_GetObjectItem(root, WEA_TEMPMIN));
    info.tempmax          = cJSON_GetNumberValue(cJSON_GetObjectItem(root, WEA_TEMPMAX));
    info.uvi              = cJSON_GetNumberValue(cJSON_GetObjectItem(root, WEA_UVI));
    info.max_uvi          = cJSON_GetNumberValue(cJSON_GetObjectItem(root, WEA_MAX_UVI));
    info.precipprob       = cJSON_GetNumberValue(cJSON_GetObjectItem(root, WEA_PRECIPPROB));
    info.precipprob_max   = cJSON_GetNumberValue(cJSON_GetObjectItem(root, WEA_PRECIPPROB_MAX));
    info.sunrise           = cJSON_GetNumberValue(cJSON_GetObjectItem(root, WEA_SUNRISE));
    info.sunset            = cJSON_GetNumberValue(cJSON_GetObjectItem(root, WEA_SUNSET));

    const char *icon = cJSON_GetStringValue(cJSON_GetObjectItem(root, WEA_ICON));
    snprintf(info.icon, sizeof(info.icon), "%s", icon ? icon : "");

    return info;
}

cJSON *serialize_weather_info(const WeatherInfo *info) {
    cJSON *out = cJSON_CreateObject();
    cJSON_AddNumberToObject(out, WEA_TEMP,           info->temp);
    cJSON_AddNumberToObject(out, WEA_TEMPMIN,        info->tempmin);
    cJSON_AddNumberToObject(out, WEA_TEMPMAX,        info->tempmax);
    cJSON_AddNumberToObject(out, WEA_UVI,            info->uvi);
    cJSON_AddNumberToObject(out, WEA_MAX_UVI,        info->max_uvi);
    cJSON_AddNumberToObject(out, WEA_PRECIPPROB,     info->precipprob);
    cJSON_AddNumberToObject(out, WEA_PRECIPPROB_MAX, info->precipprob_max);
    cJSON_AddNumberToObject(out, WEA_SUNRISE,        info->sunrise);
    cJSON_AddNumberToObject(out, WEA_SUNSET,         info->sunset);
    cJSON_AddStringToObject(out, WEA_ICON,           info->icon);
    return out;
}
