/* Unit tests for departure_info.c: which time the board shows and when a
 * departure counts as departed. Run: ctest --test-dir build-native */
#include <stdio.h>
#include <cjson/cJSON.h>
#include "departure_info.h"

static int fails;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "%s:%d: FAIL %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

static DepartureInfo dep(double planned, int delay_min, char status) {
    DepartureInfo d = {0};
    d.planned_ts = planned;
    d.changed_ts = delay_min ? planned + delay_min * 60 : 0;
    d.delay_min = delay_min;
    d.status = status;
    departure_info_derive(&d, 5);
    return d;
}

int main(void) {
    const double T = 1700000040;  /* minute-aligned planned time */
    DepartureInfo d;

    /* on time: shown until 60 s past planned, then departed */
    d = dep(T, 0, 'p');
    CHECK(!d.show_delay && d.effective_ts == T);
    CHECK(!departure_info_departed(&d, T + 60, 60));
    CHECK(departure_info_departed(&d, T + 61, 60));

    /* small delay (< threshold): counts as on time, dropped by planned time —
     * previously it lingered at "0" until changed_ts + 2 min */
    d = dep(T, 4, 'p');
    CHECK(!d.show_delay && d.effective_ts == T);
    CHECK(departure_info_departed(&d, T + 61, 60));

    /* delay >= threshold: shown delayed, kept until changed time + grace */
    d = dep(T, 7, 'p');
    CHECK(d.show_delay && d.effective_ts == T + 420);
    CHECK(!departure_info_departed(&d, T + 420 + 60, 60));
    CHECK(departure_info_departed(&d, T + 420 + 61, 60));

    /* early by >= threshold */
    d = dep(T, -5, 'p');
    CHECK(d.show_delay && d.effective_ts == T - 300);

    /* cancelled: planned time, same grace */
    d = dep(T, 10, 'c');
    CHECK(d.cancelled && !d.show_delay && d.effective_ts == T);
    CHECK(departure_info_departed(&d, T + 61, 60));

    /* no time data: never departed */
    d = dep(0, 0, 'p');
    CHECK(!departure_info_departed(&d, T, 60));

    /* JSON round trip derives the same fields */
    d = dep(T, 7, 'p');
    cJSON *j = serialize_departure_info(&d);
    DepartureInfo p = parse_departure_info(j, 5);
    cJSON_Delete(j);
    CHECK(p.show_delay && p.effective_ts == d.effective_ts);

    if (!fails) puts("test_departure_info: all passed");
    return fails != 0;
}
