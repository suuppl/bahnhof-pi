#pragma once
#include <time.h>

/* Return target brightness percent (0..100) for the given time and
 * sunrise/sunset times, using a smooth sigmoid ramp over ±BRIGHT_RAMP_S.
 * Pure function — no I/O, no side effects. */
int brightness_curve(time_t now, time_t sunrise, time_t sunset);
