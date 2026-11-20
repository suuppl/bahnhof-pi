#pragma once

/* Perform an HTTP GET. Returns a malloc'd NUL-terminated response body on
 * success (HTTP 2xx), NULL on any error. Caller must free(). */
char *http_get(const char *url);
