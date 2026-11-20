/*
 * bahnhofstafel — departure board display for Raspberry Pi framebuffer.
 * Reads JSON files written by iris_fetch and weather_fetch, renders via LVGL.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/kd.h>
#include "lvgl/lvgl.h"
#include "display.h"
#include "config.h"
#include "settings.h"

static uint32_t tick_cb(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* Hide the fbcon text cursor.
 *
 * When launched via systemd with StandardInput=null the process has no
 * controlling terminal, so open("/dev/tty") returns -1.  Instead:
 *   1. Disable cursor blink via the fbcon sysfs knob (no TTY needed).
 *   2. Read /sys/class/tty/tty0/active to find the live VT (e.g. "tty1"),
 *      open /dev/tty1 directly, send the hide-cursor escape and switch the
 *      VT to KD_GRAPHICS so fbcon stops rendering anything over the fb. */
static void hide_console_cursor(void) {
    FILE *f = fopen("/sys/class/graphics/fbcon/cursor_blink", "w");
    if (f) { fputs("0\n", f); fclose(f); }

    char tty_path[32] = "/dev/tty1";
    FILE *af = fopen("/sys/class/tty/tty0/active", "r");
    if (af) {
        char name[16] = {0};
        if (fgets(name, sizeof(name), af)) {
            name[strcspn(name, "\n")] = '\0';
            snprintf(tty_path, sizeof(tty_path), "/dev/%s", name);
        }
        fclose(af);
    }

    int fd = open(tty_path, O_RDWR);
    if (fd >= 0) {
        write(fd, "\033[?25l", 6);
        ioctl(fd, KDSETMODE, KD_GRAPHICS);
        close(fd);
    }
}

int main(void) {
    hide_console_cursor();

    lv_init();
    lv_tick_set_cb(tick_cb);

    lv_display_t *disp = lv_linux_fbdev_create();
    lv_linux_fbdev_set_file(disp, CFG_S(FB_DEVICE));

    display_init();
    display_run();   /* never returns */
    return 0;
}
