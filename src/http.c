#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "http.h"

typedef struct { char *data; size_t len; } Buf;

static size_t write_cb(char *ptr, size_t sz, size_t nmemb, void *ud) {
    Buf *b = ud;
    size_t n = sz * nmemb;
    b->data = realloc(b->data, b->len + n + 1);
    memcpy(b->data + b->len, ptr, n);
    b->len += n;
    b->data[b->len] = '\0';
    return n;
}

char *http_get(const char *url) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;
    Buf buf = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
    CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK || http_code >= 400) {
        fprintf(stderr, "http_get: GET %s → %ld (%s)\n",
                url, http_code, curl_easy_strerror(rc));
        free(buf.data);
        return NULL;
    }
    return buf.data;
}
