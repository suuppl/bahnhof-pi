/*
 * brightness_ctl — sets the HDMI monitor backlight via DDC/CI (MCCS VCP 0x10)
 * over the I2C bus, following a sunrise/sunset curve read from weather.json
 * (fixed fallback hours without the weather plugin). Optional plugin; while
 * another plugin has the screen off (screen.h) it pauses, and re-applies the
 * level when the screen comes back on.
 *
 * DDC/CI "Set VCP Feature" is a write-only I2C transaction; we never read back.
 * The vc4 HDMI I2C read channel on this Pi is unreliable (ddcutil's verify-read
 * fails), but raw writes apply correctly — so we issue the write and move on.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/ioctl.h>
#include <cjson/cJSON.h>
#include "config.h"
#include "settings.h"
#include "schema.h"
#include "brightness_curve.h"
#include "fileio.h"
#include "screen.h"

#ifndef I2C_SLAVE_FORCE
#define I2C_SLAVE_FORCE 0x0706   /* from <linux/i2c-dev.h>; defined here to avoid the dep */
#endif

/* Read sunrise/sunset (unix) from weather.json. Returns 0 on success. */
static int read_sun_times(time_t *sunrise, time_t *sunset) {
    char *buf = read_file(WEATHER_JSON);
    if (!buf) return -1;

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return -1;
    double sr = cJSON_GetNumberValue(cJSON_GetObjectItem(root, WEA_SUNRISE));
    double ss = cJSON_GetNumberValue(cJSON_GetObjectItem(root, WEA_SUNSET));
    cJSON_Delete(root);
    if (sr <= 0 || ss <= 0) return -1;
    *sunrise = (time_t)sr;
    *sunset  = (time_t)ss;
    return 0;
}

/* Write a DDC/CI Set VCP Feature command (raw, no readback). 0 on success. */
static int ddc_set_vcp(unsigned char vcp, unsigned char value) {
    unsigned char chk = 0x6E ^ 0x51 ^ 0x84 ^ 0x03 ^ vcp ^ 0x00 ^ value;
    unsigned char frame[7] = { 0x51, 0x84, 0x03, vcp, 0x00, value, chk };

    for (int attempt = 0; attempt < 3; attempt++) {
        int fd = open(CFG_S(I2C_BUS_PATH), O_RDWR);
        if (fd >= 0) {
            int ok = ioctl(fd, I2C_SLAVE_FORCE, CFG_I(DDC_I2C_ADDR)) >= 0 &&
                     write(fd, frame, sizeof frame) == (ssize_t)sizeof frame;
            close(fd);
            if (ok) return 0;
        }
        usleep(50000);
    }
    return -1;
}

static int ddc_set_brightness(int pct) {
    return ddc_set_vcp(CFG_I(VCP_BRIGHTNESS), (unsigned char)pct);
}


/* Synthetic sunrise/sunset for today using fixed fallback hours from config.h. */
static void fallback_sun_times(time_t now, time_t *sr, time_t *ss) {
    struct tm t = *localtime(&now);
    t.tm_hour = CFG_I(BRIGHT_FALLBACK_RISE_H); t.tm_min = 0; t.tm_sec = 0;
    *sr = mktime(&t);
    t = *localtime(&now);
    t.tm_hour = CFG_I(BRIGHT_FALLBACK_SET_H);  t.tm_min = 0; t.tm_sec = 0;
    *ss = mktime(&t);
}

int main(void) {
    if (access(CFG_S(I2C_BUS_PATH), W_OK) != 0) {
        fprintf(stderr, "brightness_ctl: cannot access %s — exiting (brightness unchanged)\n",
                CFG_S(I2C_BUS_PATH));
        return 1;
    }
    int write_failing = 0;
    time_t last_sun_update = 0;
    time_t sr = 0, ss = 0;
    int last_pct = -1;

    for (;;) {
        time_t now = time(NULL);

        /* Refresh sun times every BRIGHT_INTERVAL_S seconds. */
        if (now - last_sun_update >= CFG_I(BRIGHT_INTERVAL_S)) {
            if (read_sun_times(&sr, &ss) != 0)
                fallback_sun_times(now, &sr, &ss);
            last_sun_update = now;
        }

        /* Apply the curve while the screen is on. Blanking itself is done by
         * the display (FBIOBLANK) from screen.d requests. */
        if (screen_wanted_on()) {
            int pct = brightness_curve(now, sr, ss);
            if (pct != last_pct) {
                if (ddc_set_brightness(pct) == 0) {
                    printf("brightness_ctl: %d%%\n", pct);
                    fflush(stdout);
                    last_pct = pct;
                    write_failing = 0;
                } else if (!write_failing) {   /* log once, keep retrying quietly */
                    fprintf(stderr, "brightness_ctl: brightness i2c write failed (retrying)\n");
                    write_failing = 1;
                }
            }
        } else {
            last_pct = -1;   /* force re-apply after wake */
        }

        sleep(1);
    }
    return 0;
}
