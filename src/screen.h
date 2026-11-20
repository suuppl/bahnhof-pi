#pragma once
/* Screen on/off requests — a tiny plugin interface.
 *
 * Any plugin may drop a file into SCREEN_DIR (/run/bahnhofstafel/screen.d/),
 * e.g. presence_ctl writes 50-presence.json: { "on": false }. The screen is
 * ON unless at least one file says "on": false. Removing your file (or the
 * plugin never running) leaves the screen on. See docs/plugins.md. */

/* Write SCREEN_DIR/<name>.json with {"on": on} atomically. 0 on success. */
int screen_request(const char *name, int on);

/* 1 if no plugin currently requests the screen off, else 0. */
int screen_wanted_on(void);
