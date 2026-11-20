#include "settings.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int g_loaded;

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

/* Parse KEY=value lines into the environment without overriding variables
 * that are already set (so the real environment wins over the file). */
static void load_file(void) {
    g_loaded = 1;
    char path[512];
    const char *conf = getenv("BAHNHOF_CONF");
    if (conf) snprintf(path, sizeof(path), "%s", conf);
    else {
        const char *app = getenv("APP_DIR");
        snprintf(path, sizeof(path), "%s/bahnhofstafel.conf", app ? app : APP_DIR);
    }
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[1024];
    int n = 0;
    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line);
        if (!*s || *s == '#') continue;
        if (strncmp(s, "export ", 7) == 0) s = trim(s + 7);
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim(s), *val = trim(eq + 1);
        char *close;
        if ((val[0] == '"' || val[0] == '\'') && (close = strchr(val + 1, val[0]))) {
            *close = '\0';                      /* "quoted"  # comment */
            val++;
        } else {
            char *hash = strstr(val, " #");      /* trailing comment */
            if (hash) { *hash = '\0'; val = trim(val); }
        }
        if (*key) { setenv(key, val, 0); n++; }
    }
    fclose(f);
    fprintf(stderr, "settings: %d entries from %s\n", n, path);
}

const char *cfg_str(const char *key, const char *dflt) {
    if (!g_loaded) load_file();
    const char *v = getenv(key);
    return (v && *v) ? v : dflt;
}

long cfg_long(const char *key, long dflt) {
    const char *v = cfg_str(key, NULL);
    if (!v) return dflt;
    char *end;
    long n = strtol(v, &end, 0);          /* base 0: "0x37" works too */
    if (end == v) {
        fprintf(stderr, "settings: %s=\"%s\" is not a number, using %ld\n", key, v, dflt);
        return dflt;
    }
    return n;
}

const char *cfg_app_path(const char *rel) {
    /* small cache so repeated calls don't leak */
    static struct { const char *rel; char *path; } cache[16];
    for (int i = 0; i < 16 && cache[i].rel; i++)
        if (strcmp(cache[i].rel, rel) == 0) return cache[i].path;
    const char *app = cfg_str("APP_DIR", APP_DIR);
    size_t len = strlen(app) + strlen(rel) + 2;
    char *p = malloc(len);
    if (!p) return rel;
    snprintf(p, len, "%s/%s", app, rel);
    for (int i = 0; i < 16; i++)
        if (!cache[i].rel) { cache[i].rel = rel; cache[i].path = p; break; }
    return p;
}
