#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "fileio.h"

int write_atomic(const char *dest, const char *content) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.tmp.%d", dest, getpid());
    FILE *f = fopen(tmp, "w");
    if (!f) return -1;
    int ok = fputs(content, f) >= 0;
    fclose(f);
    if (!ok) { remove(tmp); return -1; }
    if (rename(tmp, dest) != 0) { remove(tmp); return -1; }
    return 0;
}

int copy_atomic(const char *src, const char *dst) {
    FILE *fr = fopen(src, "r");
    if (!fr) return -1;
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.tmp.%d", dst, getpid());
    FILE *fw = fopen(tmp, "w");
    if (!fw) { fclose(fr); return -1; }
    char buf[4096]; size_t n;
    int ok = 1;
    while ((n = fread(buf, 1, sizeof(buf), fr)) > 0)
        if (fwrite(buf, 1, n, fw) != n) { ok = 0; break; }
    fclose(fr);
    fclose(fw);
    if (!ok || rename(tmp, dst) != 0) { remove(tmp); return -1; }
    return 0;
}

char *read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0) { fclose(f); return NULL; }
    char *buf = malloc(sz + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);
    return buf;
}
