#pragma once

/* Write content to dest atomically (write to tmp, then rename).
 * Returns 0 on success, -1 on error (errno set). */
int write_atomic(const char *dest, const char *content);

/* Copy src to dst atomically (read src, write tmp, rename to dst).
 * Returns 0 on success, -1 on error. */
int copy_atomic(const char *src, const char *dst);

/* Read the entire file at path into a newly malloc'd, NUL-terminated
 * buffer. Caller frees. Returns NULL on error (missing file, empty file,
 * allocation failure). */
char *read_file(const char *path);
