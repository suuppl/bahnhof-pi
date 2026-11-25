/* Unit tests for departure_info.c: which time the board shows and when a
 * departure counts as departed. Run: ctest --test-dir build-native */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

    /* board order: shown time, then planned time, then destination */
    {
        DepartureInfo r[4];
        r[0] = dep(T, 7, 'p');         /* planned T, shown T+7 min */
        r[1] = dep(T + 120, 0, 'p');   /* shown T+2 min */
        r[2] = dep(T + 420, 0, 'p');   /* shown T+7 min, planned later than r[0] */
        r[3] = dep(T + 120, 0, 'p');   /* same times as r[1] */
        snprintf(r[1].destination, sizeof r[1].destination, "Zorneding");
        snprintf(r[3].destination, sizeof r[3].destination, "Erding");
        qsort(r, 4, sizeof r[0], departure_info_cmp);
        CHECK(strcmp(r[0].destination, "Erding") == 0);     /* T+2, E < Z */
        CHECK(strcmp(r[1].destination, "Zorneding") == 0);
        CHECK(r[2].planned_ts == T && r[3].planned_ts == T + 420);  /* tie at T+7: planned */
    }

    if (!fails) puts("test_departure_info: all passed");
    return fails != 0;
}
