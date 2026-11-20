/*
 * presence_ctl — screen plugin: turns the screen off after PRESENCE_TIMEOUT_S
 * seconds without a presence detection from a DFRobot C4001 mmWave radar on
 * PRESENCE_DEVICE (UART 9600 8N1, "$DFHPD,1" sentences), and back on as soon
 * as someone is detected. Writes SCREEN_DIR/50-presence.json (see screen.h);
 * the display does the actual blanking. Exits if the sensor is missing, which
 * simply leaves the screen on.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <termios.h>
#include "config.h"
#include "settings.h"
#include "screen.h"

#define SCREEN_NAME "50-presence"

/* Open the sensor UART in raw non-blocking mode. Returns fd or -1. */
static int open_uart(void) {
    int fd = open(CFG_S(PRESENCE_DEVICE), O_RDONLY | O_NOCTTY | O_NONBLOCK);
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

/* Drain available bytes; update *last_seen on every "$DFHPD,1" sentence. */
static void drain(int fd, time_t *last_seen) {
    static char buf[128];
    static int  len = 0;
    char ch;
    while (read(fd, &ch, 1) == 1) {
        if (ch == '\n') {
            buf[len] = '\0';
            if (strncmp(buf, "$DFHPD,1", 8) == 0) *last_seen = time(NULL);
            len = 0;
        } else if (ch != '\r' && len < (int)sizeof(buf) - 1) {
            buf[len++] = ch;
        }
    }
}

int main(void) {
    int fd = open_uart();
    if (fd < 0) {
        fprintf(stderr, "presence_ctl: cannot open %s — exiting (screen stays on)\n",
                CFG_S(PRESENCE_DEVICE));
        return 1;
    }
    long timeout = CFG_I(PRESENCE_TIMEOUT_S);
    time_t last_seen = time(NULL);   /* assume presence at startup */
    int last = -1;

    for (;;) {
        drain(fd, &last_seen);
        int present = (time(NULL) - last_seen) <= timeout;
        if (present != last) {
            if (screen_request(SCREEN_NAME, present) == 0) {
                printf("presence_ctl: %s\n", present ? "present -> screen on" : "away -> screen off");
                fflush(stdout);
                last = present;
            }
        }
        sleep(1);
    }
}
