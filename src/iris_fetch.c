/*
 * iris_fetch — periodically fetches DB IRIS-TTS timetable data for the
 * configured station and writes /run/bahnhofstafel/departures.json atomically.
 *
 * Ported from https://gitlab.com/bahnvorhersage/bahnvorhersage api/iris.py
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <cjson/cJSON.h>
#include "config.h"
#include "http.h"
#include "fileio.h"
#include "schema.h"
#include "departure_info.h"

/* ── types ──────────────────────────────────────────────────────────────── */

typedef struct {
    char id[128];
    char category[16];       /* tl@c: ICE, IC, RE, S, … */
    char number[16];         /* tl@n */
    char line[16];           /* dp@l (S-Bahn line number) */
    time_t planned_ts;       /* dp@pt parsed */
    time_t changed_ts;       /* dp@ct parsed, 0 if absent */
    char planned_platform[16];
    char changed_platform[16];
    char destination[192];   /* last element of dp@ppth */
    char status;             /* 'p'=planned, 'a'=added, 'c'=cancelled */
    int  delay_min;          /* (changed_ts - planned_ts)/60, 0 if none */
    int  hidden;             /* dp@hi == 1 → don't show */
} Departure;

static Departure g_deps[MAX_DEPARTURES];
static int       g_dep_count = 0;
static long      g_eva = STATION_EVA_FALLBACK;

/* ── time helpers ───────────────────────────────────────────────────────── */

/* Parse "YYMMddHHmm" (10 chars) → unix timestamp, 0 on error */
static time_t parse_iris_time(const char *s) {
    if (!s || strlen(s) < 10) return 0;
    struct tm t = {0};
    t.tm_year = 100 + (s[0]-'0')*10 + (s[1]-'0');
    t.tm_mon  = (s[2]-'0')*10 + (s[3]-'0') - 1;
    t.tm_mday = (s[4]-'0')*10 + (s[5]-'0');
    t.tm_hour = (s[6]-'0')*10 + (s[7]-'0');
    t.tm_min  = (s[8]-'0')*10 + (s[9]-'0');
    t.tm_isdst = -1;
    return mktime(&t);
}

/* Format unix timestamp as "HH:MM" into buf (at least 6 bytes) */
static void fmt_hhmm(time_t ts, char *buf) {
    struct tm *t = localtime(&ts);
    snprintf(buf, 6, "%02d:%02d", t->tm_hour, t->tm_min);
}

/* ── XML attribute helper ───────────────────────────────────────────────── */

static const char *xattr(xmlNodePtr n, const char *name) {
    xmlChar *v = xmlGetProp(n, (xmlChar *)name);
    return (const char *)v;  /* xmlFree needed; caller handles */
}

/* last pipe-delimited token from a ppth string */
static void extract_destination(const char *ppth, char *out, size_t outsz) {
    if (!ppth || !*ppth) { strncpy(out, "–", outsz); return; }
    const char *last = strrchr(ppth, '|');
    strncpy(out, last ? last + 1 : ppth, outsz - 1);
    out[outsz - 1] = '\0';
}

/* Train label for the pill: "S3", "RE8", "ICE1234". IRIS is inconsistent
 * about dp@l: it may be the full line ("S3"), only the line number ("8"),
 * just the category ("RE"), or missing. Rules:
 *   l has letters + digits      → l as-is          ("S3", "RE8")
 *   l is digits only            → category + l     ("RE" + "8")
 *   l has no digits / is empty  → category + train number ("RE" + "4012")
 */
static void make_train_label(const Departure *d, char *out, size_t outsz) {
    const char *l = d->line;
    int has_digit = 0, has_alpha = 0;
    for (const char *p = l; *p; p++) {
        if (*p >= '0' && *p <= '9') has_digit = 1;
        else if (*p != ' ') has_alpha = 1;
    }
    if (has_digit && has_alpha)
        snprintf(out, outsz, "%s", l);
    else if (has_digit)
        snprintf(out, outsz, "%s%s", d->category, l);
    else
        snprintf(out, outsz, "%s%s", d->category[0] ? d->category : l, d->number);
}

/* ── station EVA lookup ─────────────────────────────────────────────────── */

/* Returns the EVA number on success, 0 on any failure. */
static long lookup_eva(void) {
    char url[512];
    char *name = curl_easy_escape(NULL, STATION_NAME, 0);  /* UTF-8 → %XX */
    if (!name) return 0;
    snprintf(url, sizeof(url), "%s/%s", IRIS_STATION_URL, name);
    curl_free(name);

    char *xml_str = http_get(url);
    if (!xml_str) return 0;

    xmlDocPtr doc = xmlReadMemory(xml_str, strlen(xml_str), "sta.xml", "UTF-8", XML_PARSE_NOERROR);
    free(xml_str);
    if (!doc) return 0;

    long eva = 0;
    xmlNodePtr root = xmlDocGetRootElement(doc);
    if (root) {
        for (xmlNodePtr n = root->children; n; n = n->next) {
            if (n->type != XML_ELEMENT_NODE) continue;
            xmlChar *e = xmlGetProp(n, (xmlChar *)"eva");
            if (e) { eva = atol((char *)e); xmlFree(e); break; }
        }
        if (!eva) {
            xmlChar *e = xmlGetProp(root, (xmlChar *)"eva");
            if (e) { eva = atol((char *)e); xmlFree(e); }
        }
    }
    xmlFreeDoc(doc);
    return eva > 0 ? eva : 0;
}

static long load_cached_eva(void) {
    FILE *f = fopen(EVA_CACHE, "r");
    if (!f) return 0;
    long eva = 0;
    fscanf(f, "%ld", &eva);
    fclose(f);
    return eva > 0 ? eva : 0;
}

static void save_cached_eva(long eva) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld\n", eva);
    write_atomic(EVA_CACHE, buf);
}

/* ── plan XML parser ────────────────────────────────────────────────────── */

static void parse_plan_doc(xmlDocPtr doc) {
    xmlNodePtr root = xmlDocGetRootElement(doc);
    if (!root) return;

    for (xmlNodePtr s = root->children; s; s = s->next) {
        if (s->type != XML_ELEMENT_NODE || strcmp((char*)s->name, "s") != 0)
            continue;
        if (g_dep_count >= MAX_DEPARTURES) break;

        /* must have a <dp> child (departure event) */
        xmlNodePtr dp_node = NULL;
        xmlNodePtr tl_node = NULL;
        for (xmlNodePtr c = s->children; c; c = c->next) {
            if (c->type != XML_ELEMENT_NODE) continue;
            if (strcmp((char*)c->name, "dp") == 0) dp_node = c;
            if (strcmp((char*)c->name, "tl") == 0) tl_node = c;
        }
        if (!dp_node) continue;  /* arrival-only stop, skip */

        Departure *d = &g_deps[g_dep_count];
        memset(d, 0, sizeof(*d));
        d->status = 'p';

        /* stop id */
        xmlChar *id = xmlGetProp(s, (xmlChar *)"id");
        if (id) { strncpy(d->id, (char*)id, sizeof(d->id)-1); xmlFree(id); }

        /* trip label */
        if (tl_node) {
            xmlChar *c = xmlGetProp(tl_node, (xmlChar *)"c");
            xmlChar *n = xmlGetProp(tl_node, (xmlChar *)"n");
            if (c) { strncpy(d->category, (char*)c, sizeof(d->category)-1); xmlFree(c); }
            if (n) { strncpy(d->number,   (char*)n, sizeof(d->number)-1);   xmlFree(n); }
        }

        /* departure event attributes */
        xmlChar *pt   = xmlGetProp(dp_node, (xmlChar *)"pt");
        xmlChar *pp   = xmlGetProp(dp_node, (xmlChar *)"pp");
        xmlChar *ppth = xmlGetProp(dp_node, (xmlChar *)"ppth");
        xmlChar *l    = xmlGetProp(dp_node, (xmlChar *)"l");
        xmlChar *hi   = xmlGetProp(dp_node, (xmlChar *)"hi");

        if (pt)   { d->planned_ts = parse_iris_time((char*)pt); xmlFree(pt); }
        if (pp)   { strncpy(d->planned_platform, (char*)pp, sizeof(d->planned_platform)-1); xmlFree(pp); }
        if (ppth) { extract_destination((char*)ppth, d->destination, sizeof(d->destination)); xmlFree(ppth); }
        if (l)    { strncpy(d->line, (char*)l, sizeof(d->line)-1); xmlFree(l); }
        if (hi)   { d->hidden = atoi((char*)hi); xmlFree(hi); }

        if (d->planned_ts == 0) continue;  /* bad data */
        g_dep_count++;
    }
}

/* ── rchg XML merger ────────────────────────────────────────────────────── */

static void merge_rchg_doc(xmlDocPtr doc) {
    xmlNodePtr root = xmlDocGetRootElement(doc);
    if (!root) return;

    for (xmlNodePtr s = root->children; s; s = s->next) {
        if (s->type != XML_ELEMENT_NODE || strcmp((char*)s->name, "s") != 0)
            continue;

        xmlChar *id = xmlGetProp(s, (xmlChar *)"id");
        if (!id) continue;

        /* find matching departure */
        Departure *d = NULL;
        for (int i = 0; i < g_dep_count; i++) {
            if (strcmp(g_deps[i].id, (char*)id) == 0) { d = &g_deps[i]; break; }
        }
        xmlFree(id);
        if (!d) continue;

        for (xmlNodePtr c = s->children; c; c = c->next) {
            if (c->type != XML_ELEMENT_NODE || strcmp((char*)c->name, "dp") != 0)
                continue;

            xmlChar *ct   = xmlGetProp(c, (xmlChar *)"ct");
            xmlChar *cp   = xmlGetProp(c, (xmlChar *)"cp");
            xmlChar *cs   = xmlGetProp(c, (xmlChar *)"cs");
            xmlChar *cpth = xmlGetProp(c, (xmlChar *)"cpth"); /* changed path */
            xmlChar *hi   = xmlGetProp(c, (xmlChar *)"hi");   /* hidden flag */

            if (ct) {
                d->changed_ts = parse_iris_time((char*)ct);
                xmlFree(ct);
            }
            if (cp) {
                strncpy(d->changed_platform, (char*)cp, sizeof(d->changed_platform)-1);
                xmlFree(cp);
            }
            if (cs) {
                /* cs is the only authoritative source for status; clt is metadata */
                d->status = ((char*)cs)[0];
                xmlFree(cs);
            }
            if (cpth && d->status != 'c') {
                /* changed path → update destination, but keep planned dest for cancelled trains */
                extract_destination((char*)cpth, d->destination, sizeof(d->destination));
                xmlFree(cpth);
            } else if (cpth) {
                xmlFree(cpth);
            }
            if (hi) {
                d->hidden = atoi((char*)hi);
                xmlFree(hi);
            }
        }

        if (d->changed_ts > 0 && d->planned_ts > 0)
            d->delay_min = (int)((d->changed_ts - d->planned_ts) / 60);
    }
}

/* ── fetch helpers ──────────────────────────────────────────────────────── */

static xmlDocPtr fetch_xml(const char *url) {
    char *raw = http_get(url);
    if (!raw) return NULL;
    xmlDocPtr doc = xmlReadMemory(raw, strlen(raw), "iris.xml", "UTF-8", XML_PARSE_NOERROR);
    free(raw);
    return doc;
}

/* ── sort by planned departure time ─────────────────────────────────────── */

static int dep_cmp(const void *a, const void *b) {
    const Departure *da = a, *db = b;
    return (da->planned_ts > db->planned_ts) - (da->planned_ts < db->planned_ts);
}

/* ── JSON serializer ────────────────────────────────────────────────────── */

/* Map the producer-internal, XML-sourced Departure onto the canonical
 * DepartureInfo that crosses the IPC boundary. Kept private to iris_fetch.c
 * (see ADR 0003) — the real and demo paths each have their own conversion
 * into DepartureInfo, since they start from differently-shaped source data;
 * only the resulting struct and its serializer are shared. */
static DepartureInfo departure_to_info(const Departure *d) {
    DepartureInfo info = {0};

    fmt_hhmm(d->planned_ts, info.planned_time);
    if (d->changed_ts > 0) fmt_hhmm(d->changed_ts, info.changed_time);

    info.planned_ts = (double)d->planned_ts;
    info.changed_ts = (double)(d->changed_ts > 0 ? d->changed_ts : 0);

    make_train_label(d, info.train, sizeof(info.train));

    snprintf(info.destination, sizeof(info.destination), "%s", d->destination);
    snprintf(info.planned_platform, sizeof(info.planned_platform), "%s", d->planned_platform);
    snprintf(info.changed_platform, sizeof(info.changed_platform), "%s", d->changed_platform);
    info.status = d->status;
    info.delay_min = d->delay_min;

    return info;
}

static void write_departures(void) {
    time_t now = time(NULL);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "fetched_at", (double)now);
    cJSON_AddStringToObject(root, "station",    STATION_NAME);
    cJSON_AddNumberToObject(root, "eva",        (double)g_eva);

    cJSON *arr = cJSON_AddArrayToObject(root, "departures");

    int written = 0;
    for (int i = 0; i < g_dep_count && written < MAX_ROWS; i++) {
        Departure *d = &g_deps[i];

        /* skip hidden stops and past departures */
        time_t eff = d->changed_ts > 0 ? d->changed_ts : d->planned_ts;
        int grace = (d->status == 'c') ? 180 : 120;  /* cancelled: 3min past planned, others: 2min past effective */
        if (d->hidden || eff < now - grace) continue;

        DepartureInfo info = departure_to_info(d);
        cJSON_AddItemToArray(arr, serialize_departure_info(&info));
        written++;
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) return;

    write_atomic(DEPARTURES_JSON, json_str);
    free(json_str);
}

/* ── main fetch cycle ───────────────────────────────────────────────────── */

static void fetch_cycle(void) {
    g_dep_count = 0;

    time_t now = time(NULL);
    char url[512];

    /* fetch plan for 1 hour ago through 10 hours ahead so the display
     * stays full even during sparse night-time service */
    for (int delta = -1; delta <= 10; delta++) {
        time_t slot = now + delta * 3600;
        struct tm *ts = localtime(&slot);
        char date[7], hour[3];
        strftime(date, sizeof(date), "%y%m%d", ts);
        snprintf(hour, sizeof(hour), "%02d", ts->tm_hour);

        snprintf(url, sizeof(url), "%s/%ld/%s/%s", IRIS_PLAN_URL, g_eva, date, hour);
        xmlDocPtr doc = fetch_xml(url);
        if (doc) { parse_plan_doc(doc); xmlFreeDoc(doc); }
    }

    /* sort by planned time */
    qsort(g_deps, g_dep_count, sizeof(Departure), dep_cmp);

    /* fchg (full changes): all known changes incl. cancellations from earlier today */
    snprintf(url, sizeof(url), "%s/%ld", IRIS_FCHG_URL, g_eva);
    xmlDocPtr fchg_doc = fetch_xml(url);
    if (fchg_doc) { merge_rchg_doc(fchg_doc); xmlFreeDoc(fchg_doc); }

    /* rchg (recent changes): may contain updates not yet in fchg */
    snprintf(url, sizeof(url), "%s/%ld", IRIS_RCHG_URL, g_eva);
    xmlDocPtr rchg_doc = fetch_xml(url);
    if (rchg_doc) { merge_rchg_doc(rchg_doc); xmlFreeDoc(rchg_doc); }

    write_departures();
    printf("iris_fetch: %d departures written to %s\n", g_dep_count, DEPARTURES_JSON);
}

/* ── demo mode: cycle canned boards to exercise the departure display ───── */

typedef struct {
    int offset_min;             /* minutes from "now" (at write time) the train is planned; DEMO_NO_TIME = no time data at all */
    int delay_min;               /* 0 = on time / no changed_ts */
    const char *train;
    const char *destination;
    const char *status;          /* "p", "a", "c" */
} DemoDep;

#define DEMO_NO_TIME (-100000)

static const DemoDep board_normal[] = {
    {3,  0, "S1",      "Hauptbahnhof",                  "p"},
    {7,  0, "S2",      "Flughafen",     "p"},
    {13, 0, "RE1",     "Nordstadt Hbf",                 "p"},
    {19, 0, "S1",      "Westend",                   "p"},
    {25, 0, "S2",      "Ostbahnhof",           "p"},
    {33, 0, "IC2000",  "Grenzstadt Hbf",                 "p"},
    {40, 0, "S1",      "Hauptbahnhof",                  "p"},
};

static const DemoDep board_delays[] = {
    {3,  0,  "S1",      "Hauptbahnhof",                  "p"},
    {7,  12, "S2",      "Flughafen",     "p"},
    {13, -3, "RE1",     "Nordstadt Hbf",                 "p"},
    {19, 5,  "S1",      "Westend",                   "p"},
    {25, 0,  "S2",      "Ostbahnhof",           "p"},
    {33, 2,  "ICE500",  "Hafenstadt",               "p"},
    {40, 0,  "S1",      "Hauptbahnhof",                  "p"},
};

/* delay reported but no changed_time (edge case), missing planned time,
 * huge delay, cancelled with/without a reported delay */
static const DemoDep board_edge_cases[] = {
    {4,  4,             "S1",      "Hauptbahnhof",              "p"},
    {8,  0,             "S2",      "Flughafen", "c"},
    {14, 9,             "RE1",     "Nordstadt Hbf",             "c"},
    {20, 0,             "S1",      "Westend",               "p"},
    {DEMO_NO_TIME, 0,   "S2",      "Ostbahnhof",       "p"},
    {30, 0,             "ICE500",  "Hafenstadt",           "p"},
    {36, 51,            "S1",      "Hauptbahnhof",              "p"},
};

static const DemoDep board_long_dest[] = {
    {3,  0,  "S1",      "Bergdorf \xc3\xbc" "ber S\xc3\xbc" "dstadt und Waldheim", "p"},
    {7,  12, "S2",      "Flughafen Besucherpark Terminal 2",     "p"},
    {13, 0,  "RE1",     "Nordstadt Hbf \xc3\xbc" "ber Neudorf",                   "p"},
    {19, 0,  "S1",      "Westend",                                           "c"},
    {25, 0,  "S2",      "Ostbahnhof \xc3\xbc" "ber Westkreuz und Mitte",     "p"},
    {33, 3,  "IC2000",  "Grenzstadt Hbf \xc3\xbc" "ber Seeburg und Talheim", "p"},
    {40, 0,  "S1",      "Hauptbahnhof",                                          "p"},
};

/* MAX_ROWS-sized board to test the full 16-row cap end to end */
static const DemoDep board_full[] = {
    {3,  0, "S1",     "Hauptbahnhof",              "p"},
    {7,  0, "S2",     "Flughafen", "p"},
    {13, 0, "RE1",    "Nordstadt Hbf",             "p"},
    {19, 5, "S1",     "Westend",               "p"},
    {25, 0, "S2",     "Ostbahnhof",       "p"},
    {33, 0, "IC2000", "Grenzstadt Hbf",             "p"},
    {40, 0, "S1",     "Hauptbahnhof",              "c"},
    {44, 0, "S4",     "Seeufer",           "p"},
    {47, 0, "S2",     "Flughafen", "p"},
    {51, 5, "RE1",    "Nordstadt Hbf",             "p"},
    {55, 0, "S1",     "Westend",               "p"},
    {61, 0, "S2",     "Ostbahnhof",       "p"},
    {67, 0, "S5",    "Messe",                   "p"},
    {70, 0, "S1",     "Hauptbahnhof",              "p"},
    {76, 3, "ICE500", "Hafenstadt",           "p"},
    {83, 0, "S2",     "Flughafen", "p"},
};

typedef struct {
    const char *label;
    const DemoDep *deps;
    int count;
} DemoBoard;

#define DEMO_BOARD(b) { #b, b, (int)(sizeof(b) / sizeof(b[0])) }

static const DemoBoard demo_boards[] = {
    DEMO_BOARD(board_normal),
    DEMO_BOARD(board_delays),
    DEMO_BOARD(board_edge_cases),
    DEMO_BOARD(board_long_dest),
    DEMO_BOARD(board_full),
};

#define DEMO_INTERVAL_S 3

static void write_demo_board(const DemoBoard *b) {
    time_t now = time(NULL);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "fetched_at", (double)now);
    cJSON_AddStringToObject(root, "station",    STATION_NAME);
    cJSON_AddNumberToObject(root, "eva",        (double)STATION_EVA_FALLBACK);

    cJSON *arr = cJSON_AddArrayToObject(root, "departures");
    for (int i = 0; i < b->count; i++) {
        const DemoDep *d = &b->deps[i];

        time_t planned_ts = (d->offset_min == DEMO_NO_TIME) ? 0 : now + d->offset_min * 60;
        time_t changed_ts = (planned_ts && d->delay_min != 0) ? planned_ts + d->delay_min * 60 : 0;

        DepartureInfo info = {0};
        if (planned_ts) fmt_hhmm(planned_ts, info.planned_time);
        else snprintf(info.planned_time, sizeof(info.planned_time), "\xe2\x80\x93");
        if (changed_ts) fmt_hhmm(changed_ts, info.changed_time);
        info.planned_ts = (double)planned_ts;
        info.changed_ts = (double)changed_ts;
        snprintf(info.train, sizeof(info.train), "%s", d->train);
        snprintf(info.destination, sizeof(info.destination), "%s", d->destination);
        info.status = d->status[0];
        info.delay_min = d->delay_min;

        cJSON_AddItemToArray(arr, serialize_departure_info(&info));
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) return;

    if (write_atomic(DEPARTURES_JSON, json_str) != 0)
        fprintf(stderr, "iris_fetch: demo failed to write %s\n", DEPARTURES_JSON);
    free(json_str);
}

static void run_demo(void) {
    mkdir(DATA_DIR, 0755);
    int n_boards = (int)(sizeof(demo_boards) / sizeof(demo_boards[0]));
    printf("iris_fetch: demo mode — cycling %d boards every %ds (Ctrl+C to stop)\n",
           n_boards, DEMO_INTERVAL_S);
    for (;;) {
        for (int i = 0; i < n_boards; i++) {
            printf("iris_fetch: demo board \"%s\" (%d departures)\n",
                   demo_boards[i].label, demo_boards[i].count);
            write_demo_board(&demo_boards[i]);
            sleep(DEMO_INTERVAL_S);
        }
    }
}

/* ── main ───────────────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--demo") == 0) {
        run_demo();
        return 0;
    }

    mkdir(DATA_DIR, 0755);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    xmlInitParser();

    /* load cached EVA, then try live lookup; update cache if lookup succeeds */
    long cached = load_cached_eva();
    if (cached) g_eva = cached;

    long looked_up = lookup_eva();
    if (looked_up) {
        if (looked_up != g_eva) save_cached_eva(looked_up);
        g_eva = looked_up;
    }
    printf("iris_fetch: station %s EVA=%ld (%s)\n", STATION_NAME, g_eva,
           looked_up ? "live" : cached ? "cached" : "fallback");

    for (;;) {
        if (!g_eva) {  /* no lookup, cache or fallback yet — keep trying */
            long eva = lookup_eva();
            if (eva) { g_eva = eva; save_cached_eva(eva); }
        }
        if (g_eva) fetch_cycle();
        else fprintf(stderr, "iris_fetch: station \"%s\" not resolved yet\n", STATION_NAME);
        sleep(IRIS_INTERVAL_S);
    }

    xmlCleanupParser();
    curl_global_cleanup();
    return 0;
}
