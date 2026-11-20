#include "screen.h"
#include "config.h"
#include "schema.h"
#include "fileio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cjson/cJSON.h>

int screen_request(const char *name, int on) {
    char path[256], body[32];
    mkdir(SCREEN_DIR, 0755);
    snprintf(path, sizeof(path), "%s/%s.json", SCREEN_DIR, name);
    snprintf(body, sizeof(body), "{\"%s\":%s}\n", SCR_ON, on ? "true" : "false");
    return write_atomic(path, body);
}

int screen_wanted_on(void) {
    DIR *d = opendir(SCREEN_DIR);
    if (!d) return 1;
    int on = 1;
    struct dirent *e;
    while (on && (e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (e->d_name[0] == '.' || l < 6 || strcmp(e->d_name + l - 5, ".json") != 0) continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", SCREEN_DIR, e->d_name);
        char *raw = read_file(path);
        cJSON *root = raw ? cJSON_Parse(raw) : NULL;
        free(raw);
        if (cJSON_IsFalse(cJSON_GetObjectItem(root, SCR_ON))) on = 0;
        cJSON_Delete(root);
    }
    closedir(d);
    return on;
}
