#include "brightness_curve.h"
#include "config.h"

static double smoothstep(double x) {
    if (x < 0) x = 0; else if (x > 1) x = 1;
    return x * x * (3.0 - 2.0 * x);
}

int brightness_curve(time_t now, time_t sunrise, time_t sunset) {
    const double day = BRIGHT_DAY_PCT, night = BRIGHT_NIGHT_PCT, W = BRIGHT_RAMP_S;
    double pct;
    if      (now < sunrise - W) pct = night;
    else if (now < sunrise + W) pct = night + (day - night) * smoothstep((double)(now - (sunrise - W)) / (2 * W));
    else if (now < sunset  - W) pct = day;
    else if (now < sunset  + W) pct = day - (day - night) * smoothstep((double)(now - (sunset  - W)) / (2 * W));
    else                        pct = night;
    int p = (int)(pct + 0.5);
    if (p < 0) p = 0; else if (p > 100) p = 100;
    return p;
}
