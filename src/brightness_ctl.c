/*
 * brightness_ctl — sets the HDMI monitor backlight via DDC/CI (MCCS VCP 0x10)
 * over the I2C bus, following a sunrise/sunset curve read from weather.json.
 * Turns the screen off after PRESENCE_TIMEOUT_S seconds without a presence
 * detection from the C4001 mmWave radar on PRESENCE_DEVICE (UART, $DFHPD).
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
#include <termios.h>
#include <sys/ioctl.h>
#include <cjson/cJSON.h>
#include "config.h"
#include "schema.h"
#include "brightness_curve.h"
#include "fileio.h"

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
        int fd = open(I2C_BUS_PATH, O_RDWR);
        if (fd >= 0) {
            int ok = ioctl(fd, I2C_SLAVE_FORCE, DDC_I2C_ADDR) >= 0 &&
                     write(fd, frame, sizeof frame) == (ssize_t)sizeof frame;
            close(fd);
            if (ok) return 0;
        }
        usleep(50000);
    }
    return -1;
}

static int ddc_set_brightness(int pct) {
    return ddc_set_vcp(VCP_BRIGHTNESS, (unsigned char)pct);
}


/* Synthetic sunrise/sunset for today using fixed fallback hours from config.h. */
static void fallback_sun_times(time_t now, time_t *sr, time_t *ss) {
    struct tm t = *localtime(&now);
    t.tm_hour = BRIGHT_FALLBACK_RISE_H; t.tm_min = 0; t.tm_sec = 0;
    *sr = mktime(&t);
    t = *localtime(&now);
    t.tm_hour = BRIGHT_FALLBACK_SET_H;  t.tm_min = 0; t.tm_sec = 0;
    *ss = mktime(&t);
}

/* Write presence state to presence.json atomically so the display can poll it. */
static void write_presence_json(int present) {
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "%s.tmp", PRESENCE_JSON);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "{\"present\":%d}\n", present);
    fclose(f);
    rename(tmp, PRESENCE_JSON);
}

/* Open the presence sensor UART in raw non-blocking mode. Returns fd or -1. */
static int open_presence_uart(void) {
    int fd = open(PRESENCE_DEVICE, O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -1;
    struct termios tty = {0};
    cfsetspeed(&tty, B9600);
    tty.c_cflag = CS8 | CREAD | CLOCAL;
    tty.c_iflag = IGNPAR;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSANOW, &tty);
    return fd;
}

/*
 * Drain all available bytes from the presence UART.
 * Updates *last_seen whenever a $DFHPD sentence with state=1 is received.
 */
static void drain_presence(int fd, time_t *last_seen) {
    static char buf[128];
    static int  len = 0;
    char ch;
    while (read(fd, &ch, 1) == 1) {
        if (ch == '\n') {
            buf[len] = '\0';
            if (strncmp(buf, "$DFHPD,1", 8) == 0)
                *last_seen = time(NULL);
            len = 0;
        } else if (ch != '\r' && len < (int)sizeof(buf) - 1) {
            buf[len++] = ch;
        }
    }
}

int main(void) {
    int uart_fd = open_presence_uart();
    if (uart_fd < 0)
        fprintf(stderr, "brightness_ctl: cannot open %s — presence detection disabled\n",
                PRESENCE_DEVICE);

    time_t last_seen      = time(NULL);   /* assume presence at startup */
    time_t last_sun_update = 0;
    time_t sr = 0, ss = 0;
    int last_pct     = -1;
    int last_present = -1;

    for (;;) {
        if (uart_fd >= 0)
            drain_presence(uart_fd, &last_seen);

        time_t now = time(NULL);

        /* Refresh sun times every BRIGHT_INTERVAL_S seconds. */
        if (now - last_sun_update >= BRIGHT_INTERVAL_S) {
            if (read_sun_times(&sr, &ss) != 0)
                fallback_sun_times(now, &sr, &ss);
            last_sun_update = now;
        }

        int present = (now - last_seen) <= PRESENCE_TIMEOUT_S;
        if (present != last_present) {
            write_presence_json(present);
            last_present = present;
        }

        /* Apply DDC brightness curve while someone is present.
         * Blanking/unblanking is handled by the display process via FBIOBLANK
         * so it can sequence the redraw immediately after wake. */
        if (present) {
            int pct = brightness_curve(now, sr, ss);
            if (pct != last_pct) {
                if (ddc_set_brightness(pct) == 0) {
                    printf("brightness_ctl: %d%%\n", pct);
                    fflush(stdout);
                    last_pct = pct;
                } else {
                    fprintf(stderr, "brightness_ctl: brightness i2c write failed\n");
                }
            }
        } else {
            last_pct = -1;   /* force re-apply after wake */
        }

        sleep(1);
    }
    return 0;
}
