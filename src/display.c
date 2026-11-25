/*
 * display.c — LVGL UI for the departure board.
 * All LVGL calls happen on the main thread via timers (no locking needed).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include "lvgl/lvgl.h"
#include "lvgl/src/libs/freetype/lv_freetype.h"
#include <cjson/cJSON.h>
#include "config.h"
#include "settings.h"
#include "display.h"
#include "schema.h"
#include "departure_info.h"
#include "fileio.h"
#include "screen.h"

/* ── layout constants ───────────────────────────────────────────────────── */

#define HDR_BAR_H 132

#define COL_TIME   130
#define COL_CLOCK  150  /* absolute departure time (HH:MM) column */
#define COL_TRAIN  300  /* fixed slot the (content-width) train pill is centered in */
#define COL_PLAT   170  /* platform column at the right edge (SHOW_PLATFORM) */

/* weather grid — taller than the header to fit temp/UV/rain stacked in two
 * rows (main value + sub-row) plus the condition icon, while still leaving
 * room for a full 7th departure row on the 1920x1200 display (measured:
 * row_h=129px, so header(132)+weather+7*129 <= 1200 requires BAR_H<=165). */
#define C_BORDER   0x44475a
#define BAR_H  163

/* font paths — FreeType reads directly from the filesystem (no drive prefix) */
/* FONT_PATH / FONT_BOLD_PATH / EMOJI_FONT_PATH: runtime settings (config.h) */

/* colors — Dracula theme */
#define C_BG        0x282a36   /* background */
#define C_TEXT      0xf8f8f2   /* foreground */
#define C_DIM       0x6272a4   /* comment */
#define C_GREY      0x6e6e6e   /* cancelled-train pill background */
#define C_DELAY     0xffb86c   /* orange */
#define C_ONTIME    0x50fa7b   /* green */
#define C_HEADER    0x44475a   /* current line */
#define C_COLHDR    0x343746
#define C_ACCENT    0xbd93f9   /* purple */
#define C_BAR   C_HEADER
#define C_DOT_ON    0x50fa7b   /* green */
#define C_DOT_OFF   0x44475a   /* current line */

/* ── fonts (created once, kept alive forever) ───────────────────────────── */

static lv_font_t *font_hdr;       /* 72px — clock */
static lv_font_t *font_hdr_bold;  /* 72px bold — station name */
static lv_font_t *font_row;       /* 60px — departure rows */
static lv_font_t *font_col;       /* 36px — column headers + planned time */
static lv_font_t *font_wea;       /* 48px — weather ticker */
static lv_font_t *font_emoji_wea; /* 48px — Noto Color Emoji, fallback for weather */
static lv_font_t *font_emoji_hdr; /* 72px — Noto Color Emoji, fallback for header */
static lv_font_t *font_emoji_icon; /* 130px — larger emoji for condition icon */
static lv_font_t *font_emoji_row; /* 60px — Noto Color Emoji, fallback for rows */
static lv_font_t *font_emoji_col; /* 36px — Noto Color Emoji, fallback for col */

/* ── widget handles ─────────────────────────────────────────────────────── */

static lv_obj_t *hours_lbl;
static lv_obj_t *colon_lbl;
static lv_obj_t *min_lbl;
static lv_obj_t *date_lbl;
static lv_obj_t *stn_lbl;

/* bottom bar: filled entirely by plugins (see docs/plugins.md). Every
 * *.json in BAR_DIR contributes groups, in filename order; groups with
 * "align":"right" go into the right-hand box. Widgets are (re)built lazily
 * whenever the combined layout changes. */
#define BAR_MAX_GROUPS 16
#define BAR_MAX_SUB    2
#define BAR_MAX_FILES  16
typedef struct {
    lv_obj_t *outer;
    lv_obj_t *top;            /* value label, or the big icon label */
    lv_obj_t *sub[BAR_MAX_SUB];
    int nsub;
} BarGroup;
static lv_obj_t *g_bar_left;
static lv_obj_t *g_bar_right;
static BarGroup  g_bar[BAR_MAX_GROUPS];
static int       g_bar_n = -1;
static char      g_bar_sig[2048];   /* layout signature of the built groups */
static char      g_bar_files[1024]; /* "name:mtime;..." of the last scan */
static void bar_poll_cb(lv_timer_t *t);

/* One departure row's LVGL widgets, plus the pair of functions that own it
 * (departure_row_create/departure_row_render, defined below) — used by both
 * the real data path and the startup placeholder so they can't independently
 * drift into different rendering rules the way they once did (ADR 0002). */
typedef struct {
    lv_obj_t *row;
    lv_obj_t *time_box;     /* flex-column, COL_TIME wide */
    lv_obj_t *time_lbl;
    lv_obj_t *clock_lbl;    /* absolute departure time "HH:MM" */
    lv_obj_t *planned_lbl;  /* planned time "(HH:MM)", hidden when on-time */
    lv_obj_t *train_pill;   /* pill container around train_lbl */
    lv_obj_t *train_lbl;
    lv_obj_t *dest_lbl;
    lv_obj_t *plat_lbl;     /* platform, right column; hidden unless SHOW_PLATFORM */
} DepartureRow;

static DepartureRow g_rows[MAX_ROWS];

static int g_row_count = 0;
static int g_max_rows  = MAX_ROWS;  /* capped after layout to whole rows only */
static int g_has_real_data    = 0;
static int g_screen_on        = -1;   /* -1 = unknown, 0 = blanked, 1 = on */

/* ── file-change detection ──────────────────────────────────────────────── */

static time_t g_dep_mtime  = 0;

static time_t file_mtime(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 ? st.st_mtime : 0;
}

/* ── style helpers ──────────────────────────────────────────────────────── */

static void set_text_color(lv_obj_t *obj, uint32_t hex) {
    lv_obj_set_style_text_color(obj, lv_color_hex(hex), 0);
}

static void set_bg_color(lv_obj_t *obj, uint32_t hex) {
    lv_obj_set_style_bg_color(obj, lv_color_hex(hex), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
}

/* ── row visibility ─────────────────────────────────────────────────────── */

static void show_rows(int n) {
    if (n > g_max_rows) n = g_max_rows;
    for (int i = 0; i < MAX_ROWS; i++)
        lv_obj_set_style_opa(g_rows[i].row, i < n ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    g_row_count = n;
}

/* Train line → pill colours, from LINE_COLORS_JSON (optional, reloaded when
 * it changes; see docs/line-colors.md):
 *   { "S1": "#16bae7", "S8": { "bg": "#000000", "fg": "#f0ac32" }, "RE": "#ec0016" }
 * Lookup: exact train label ("S1") → its type, i.e. the leading letters
 * ("S", "RE", "ICE") → built-in default for that type → grey. Without "fg"
 * the text is black or white, whichever contrasts better. */
#define LC_MAX 64
typedef struct { char key[16]; uint32_t bg; uint32_t fg; } LineColor;
static LineColor g_lc[LC_MAX];
static int       g_lc_n;
static time_t    g_lc_mtime = -1;

static const LineColor LC_DEFAULTS[] = {   /* per train type */
    { "S",   0x408335, 0xffffff },  /* S-Bahn green */
    { "U",   0x1455c0, 0xffffff },  /* U-Bahn blue */
    { "RE",  0xec0016, 0xffffff },  /* regional red */
    { "RB",  0xec0016, 0xffffff },
    { "IRE", 0xec0016, 0xffffff },
    { "MEX", 0xec0016, 0xffffff },
    { "ICE", 0xf8f8f2, 0x282a36 },  /* long-distance white */
    { "IC",  0xf8f8f2, 0x282a36 },
    { "EC",  0xf8f8f2, 0x282a36 },
    { "ECE", 0xf8f8f2, 0x282a36 },
    { "RJ",  0xf8f8f2, 0x282a36 },
    { "NJ",  0xf8f8f2, 0x282a36 },
};

static uint32_t auto_fg(uint32_t bg) {
    int r = (bg >> 16) & 0xff, g = (bg >> 8) & 0xff, b = bg & 0xff;
    return (r * 299 + g * 587 + b * 114) / 1000 > 150 ? 0x000000 : 0xffffff;
}

/* "#rrggbb" / "rrggbb" → 0xrrggbb; -1 if malformed */
static long parse_hex_color(const char *s) {
    if (!s) return -1;
    if (*s == '#') s++;
    if (strlen(s) != 6) return -1;
    char *end;
    long v = strtol(s, &end, 16);
    return *end ? -1 : v;
}

static void load_line_colors(void) {
    time_t mt = file_mtime(LINE_COLORS_JSON);
    if (mt == g_lc_mtime) return;
    g_lc_mtime = mt;
    g_lc_n = 0;
    char *raw = mt ? read_file(LINE_COLORS_JSON) : NULL;
    cJSON *root = raw ? cJSON_Parse(raw) : NULL;
    free(raw);
    if (raw && !root) fprintf(stderr, "display: %s is not valid JSON\n", LINE_COLORS_JSON);
    const cJSON *e;
    cJSON_ArrayForEach(e, root) {
        if (g_lc_n >= LC_MAX || !e->string) break;
        long bg = parse_hex_color(cJSON_IsString(e) ? e->valuestring
                                  : cJSON_GetStringValue(cJSON_GetObjectItem(e, "bg")));
        if (bg < 0) { fprintf(stderr, "display: bad colour for \"%s\"\n", e->string); continue; }
        long fg = cJSON_IsObject(e) ? parse_hex_color(cJSON_GetStringValue(cJSON_GetObjectItem(e, "fg"))) : -1;
        LineColor *c = &g_lc[g_lc_n++];
        snprintf(c->key, sizeof(c->key), "%s", e->string);
        c->bg = (uint32_t)bg;
        c->fg = fg >= 0 ? (uint32_t)fg : auto_fg((uint32_t)bg);
    }
    cJSON_Delete(root);
}

static const LineColor *find_lc(const LineColor *t, int n, const char *key) {
    for (int i = 0; i < n; i++)
        if (strcmp(t[i].key, key) == 0) return &t[i];
    return NULL;
}

static const LineColor *line_color(const char *train) {
    if (!train || !*train) return NULL;
    const LineColor *c = find_lc(g_lc, g_lc_n, train);
    if (c) return c;
    char type[16];
    size_t i = 0;
    while (train[i] && i < sizeof(type) - 1 &&
           ((train[i] >= 'A' && train[i] <= 'Z') || (train[i] >= 'a' && train[i] <= 'z'))) {
        type[i] = train[i]; i++;
    }
    type[i] = '\0';
    if ((c = find_lc(g_lc, g_lc_n, type))) return c;
    return find_lc(LC_DEFAULTS, (int)(sizeof(LC_DEFAULTS) / sizeof(LC_DEFAULTS[0])), type);
}

static uint32_t train_pill_color(const char *train) {
    const LineColor *c = line_color(train);
    return c ? c->bg : C_GREY;
}

static uint32_t train_pill_text_color(const char *train) {
    const LineColor *c = line_color(train);
    return c ? c->fg : C_TEXT;
}

/* ── DepartureRow rendering ──────────────────────────────────────────────── */

/* The single place that turns a DepartureInfo into row widget state — used
 * by both the real data path (update_departures) and the startup placeholder
 * (init_dummy_data), so they can't drift into different rendering rules
 * (grey+strikethrough for cancelled, line pill colours, ...) the
 * way they once did (ADR 0002). */
static void departure_row_render(DepartureRow *row, DepartureInfo info) {
    /* time: minutes until departure, plus the absolute HH:MM clock time */
    char time_str[24];   /* fits any long */
    char clock_str[8];

    /* Delays below DELAY_THRESHOLD_MIN are ignored (departure_info_derive):
     * effective_ts is then the planned time, shown as if on time. */
    bool show_delay = info.show_delay;

    if (info.effective_ts > 0) {
        /* Both sides rounded down to whole minutes before subtracting:
         * effective_ts is already minute-aligned (parsed from IRIS's HH:MM),
         * but time(NULL) carries seconds, which previously caused the
         * displayed count to round to the *nearer* minute and flip early
         * (e.g. showing "7" once 45s into the 6-minutes-remaining mark).
         * Truncating now to its minute matches how station boards count. */
        long mins = (long)info.effective_ts / 60 - time(NULL) / 60;
        if (mins < 0) mins = 0;
        snprintf(time_str, sizeof(time_str), "%ld", mins);

        time_t eff_time = (time_t)info.effective_ts;
        struct tm *t = localtime(&eff_time);
        snprintf(clock_str, sizeof(clock_str), "%02d:%02d", t->tm_hour, t->tm_min);
    } else {
        snprintf(time_str, sizeof(time_str), "\xe2\x80\x93");
        snprintf(clock_str, sizeof(clock_str), "\xe2\x80\x93");
    }

    lv_label_set_text(row->time_lbl, time_str);
    lv_label_set_text(row->clock_lbl, clock_str);
    lv_label_set_text(row->train_lbl, info.train[0] ? info.train : "?");
    set_text_color(row->train_lbl, info.cancelled ? C_TEXT : train_pill_text_color(info.train));
    uint32_t pill_bg = info.cancelled ? C_HEADER : train_pill_color(info.train);
    lv_obj_set_style_bg_color(row->train_pill, lv_color_hex(pill_bg), 0);
    lv_label_set_text(row->dest_lbl, info.destination[0] ? info.destination : "\xe2\x80\x93");

    if (show_delay && info.planned_time[0]) {
        char plan_str[32];
        snprintf(plan_str, sizeof(plan_str), "(%s)", info.planned_time);
        lv_label_set_text(row->planned_lbl, plan_str);
        lv_obj_clear_flag(row->planned_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        /* hidden (not just transparent) so it's excluded from clock_box's
         * layout, letting the clock time center vertically on its own */
        lv_obj_add_flag(row->planned_lbl, LV_OBJ_FLAG_HIDDEN);
    }

    /* cancelled: grey out + strike through time/destination (pill background
     * turns grey too, but its white text is never struck through or recolored);
     * otherwise orange = delayed/early by >= DELAY_THRESHOLD_MIN, white = else. */
    uint32_t c = info.cancelled ? C_DIM : (show_delay ? C_DELAY : C_TEXT);
    lv_text_decor_t decor = info.cancelled ? LV_TEXT_DECOR_STRIKETHROUGH : LV_TEXT_DECOR_NONE;

    set_text_color(row->time_lbl,  c);
    set_text_color(row->clock_lbl, c);
    set_text_color(row->dest_lbl,  c);

    lv_obj_set_style_text_decor(row->time_lbl,  decor, 0);
    lv_obj_set_style_text_decor(row->clock_lbl, decor, 0);
    lv_obj_set_style_text_decor(row->dest_lbl,  decor, 0);

    /* platform: the changed one in orange if it differs from the plan */
    int plat_changed = info.changed_platform[0] &&
                       strcmp(info.changed_platform, info.planned_platform) != 0;
    const char *plat = plat_changed ? info.changed_platform : info.planned_platform;
    lv_label_set_text(row->plat_lbl, plat);
    set_text_color(row->plat_lbl, info.cancelled ? C_DIM : plat_changed ? C_DELAY : C_TEXT);
    lv_obj_set_style_text_decor(row->plat_lbl, decor, 0);
}

/* ── departure update ───────────────────────────────────────────────────── */

static void update_departures(const char *json_str) {
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return;

    cJSON *arr = cJSON_GetObjectItem(root, "departures");
    int n = cJSON_IsArray(arr) ? cJSON_GetArraySize(arr) : 0;

    if (!g_has_real_data && n > 0) {
        g_has_real_data = 1;
        if (stn_lbl) lv_label_set_text(stn_lbl, CFG_S(STATION_NAME));
    }

    /* also drop departed rows here: iris_fetch rewrites the file only once
     * per fetch cycle (>60 s), the display re-renders every DEP_POLL_MS */
    time_t now = time(NULL);
    int grace = CFG_I(DEPARTED_GRACE_S), shown = 0;
    cJSON *dep;
    cJSON_ArrayForEach(dep, arr) {
        if (shown >= MAX_ROWS) break;
        DepartureInfo info = parse_departure_info(dep, CFG_I(DELAY_THRESHOLD_MIN));
        if (departure_info_departed(&info, now, grace)) continue;
        departure_row_render(&g_rows[shown++], info);
    }

    show_rows(shown);
    cJSON_Delete(root);
}


/* ── LVGL timer callbacks ───────────────────────────────────────────────── */

static void dep_poll_cb(lv_timer_t *t) {
    (void)t;
    /* Unlike the bar plugins, recompute every tick regardless of whether
     * iris_fetch wrote new data: the "minutes until" label depends on the
     * current wall clock, not just the JSON contents, so it must stay
     * fresh between the ~60s upstream IRIS polls too. */
    load_line_colors();
    time_t mt = file_mtime(DEPARTURES_JSON);
    if (mt == 0) return;
    g_dep_mtime = mt;
    char *raw = read_file(DEPARTURES_JSON);
    if (raw) { update_departures(raw); free(raw); }
}

static void fb_blank(int blank) {
    int fd = open(CFG_S(FB_DEVICE), O_RDWR);
    if (fd < 0) return;
    ioctl(fd, FBIOBLANK, blank ? FB_BLANK_POWERDOWN : FB_BLANK_UNBLANK);
    close(fd);
}

/* Blank/unblank the framebuffer as the screen.d plugins request (screen.h). */
static void screen_poll_cb(lv_timer_t *t) {
    (void)t;
    int on = screen_wanted_on();
    if (on == g_screen_on) return;
    g_screen_on = on;
    if (on) {
        fb_blank(0);
        lv_obj_invalidate(lv_screen_active());
    } else {
        fb_blank(1);
    }
}

static void clock_cb(lv_timer_t *t) {
    (void)t;
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char buf[16];

    strftime(buf, sizeof(buf), "%d.%m.%Y", tm);
    lv_label_set_text(date_lbl, buf);

    strftime(buf, sizeof(buf), "%H", tm);
    lv_label_set_text(hours_lbl, buf);

    strftime(buf, sizeof(buf), "%M", tm);
    lv_label_set_text(min_lbl, buf);

    lv_obj_set_style_opa(colon_lbl, (now % 2 == 0) ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
}

/* ── UI construction ────────────────────────────────────────────────────── */

static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                             lv_font_t *font, uint32_t color) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    set_text_color(l, color);
    lv_obj_set_style_pad_all(l, 0, 0);
    return l;
}

/* Like make_label(), but pins the label to a fixed pixel width so the
 * bar bar's columns don't reflow sideways as their text changes
 * length (e.g. "5" -> "-19" -> "100"). Width should cover the widest
 * realistic value for that label; text is left-aligned within it so it
 * stays glued to the icon/arrow immediately to its left. */
static lv_obj_t *make_value_label(lv_obj_t *parent, const char *text,
                                   lv_font_t *font, uint32_t color, int width) {
    lv_obj_t *l = make_label(parent, text, font, color);
    lv_obj_set_width(l, width);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_LEFT, 0);
    return l;
}

static lv_obj_t *make_row(lv_obj_t *parent, uint32_t bg_color) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_ver(row, 19, 0);
    lv_obj_set_style_pad_hor(row, 30, 0);
    lv_obj_set_style_pad_column(row, 80, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    set_bg_color(row, bg_color);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    return row;
}

/* Build one departure row's widget tree. Hidden (transparent) until data
 * arrives; see show_rows(). */
static DepartureRow departure_row_create(lv_obj_t *parent, uint32_t bg_color) {
    DepartureRow r = {0};
    r.row = make_row(parent, bg_color);

    /* time column: fixed width, centered, so it doesn't shift when the
     * minute count changes digits */
    r.time_box = lv_obj_create(r.row);
    lv_obj_set_width(r.time_box, COL_TIME);
    lv_obj_set_height(r.time_box, LV_SIZE_CONTENT);
    lv_obj_set_layout(r.time_box, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(r.time_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r.time_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(r.time_box, 0, 0);
    lv_obj_set_style_border_width(r.time_box, 0, 0);
    lv_obj_set_style_radius(r.time_box, 0, 0);
    lv_obj_set_style_bg_opa(r.time_box, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(r.time_box, LV_SCROLLBAR_MODE_OFF);

    r.time_lbl = make_label(r.time_box, "", font_row, C_TEXT);

    /* absolute departure time (HH:MM), fixed width, centered; the original
     * planned time sits below it (in parens) when the train is off-schedule */
    lv_obj_t *clock_box = lv_obj_create(r.row);
    lv_obj_set_width(clock_box, COL_CLOCK);
    lv_obj_set_height(clock_box, LV_SIZE_CONTENT);
    lv_obj_set_layout(clock_box, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(clock_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(clock_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(clock_box, 0, 0);
    lv_obj_set_style_pad_row(clock_box, 2, 0);
    lv_obj_set_style_border_width(clock_box, 0, 0);
    lv_obj_set_style_radius(clock_box, 0, 0);
    lv_obj_set_style_bg_opa(clock_box, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(clock_box, LV_SCROLLBAR_MODE_OFF);

    r.clock_lbl = make_label(clock_box, "", font_col, C_TEXT);
    r.planned_lbl = make_label(clock_box, "", font_col, C_DIM);
    lv_obj_add_flag(r.planned_lbl, LV_OBJ_FLAG_HIDDEN);

    /* fixed-width slot so the destination column always starts at the
     * same x; the pill inside it still hugs the label width */
    lv_obj_t *train_col = lv_obj_create(r.row);
    lv_obj_remove_style_all(train_col);
    lv_obj_set_size(train_col, COL_TRAIN, LV_SIZE_CONTENT);
    lv_obj_set_layout(train_col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(train_col, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(train_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(train_col, LV_SCROLLBAR_MODE_OFF);

    /* pill badge for line identifier (S1, RE1, …); hugs the label width */
    r.train_pill = lv_obj_create(train_col);
    lv_obj_remove_style_all(r.train_pill);
    lv_obj_set_size(r.train_pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(r.train_pill, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(r.train_pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r.train_pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(r.train_pill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r.train_pill, lv_color_hex(C_HEADER), 0);
    lv_obj_set_style_radius(r.train_pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(r.train_pill, 22, 0);
    lv_obj_set_style_pad_ver(r.train_pill, 6, 0);
    lv_obj_set_scrollbar_mode(r.train_pill, LV_SCROLLBAR_MODE_OFF);
    r.train_lbl = make_label(r.train_pill, "", font_row, C_TEXT);

    /* destination: takes all remaining row space (also pushing planned_lbl
     * to the right edge); scrolls horizontally if the text doesn't fit */
    r.dest_lbl = make_label(r.row, "", font_row, C_TEXT);
    lv_obj_set_flex_grow(r.dest_lbl, 1);
    lv_label_set_long_mode(r.dest_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_anim_duration(r.dest_lbl, 12000, 0);

    /* platform: fixed-width right column, right-aligned */
    r.plat_lbl = make_label(r.row, "", font_row, C_TEXT);
    lv_obj_set_width(r.plat_lbl, COL_PLAT);
    lv_obj_set_style_text_align(r.plat_lbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_pad_right(r.plat_lbl, 30, 0);
    if (!CFG_I(SHOW_PLATFORM)) lv_obj_add_flag(r.plat_lbl, LV_OBJ_FLAG_HIDDEN);

    /* hidden until data arrives */
    lv_obj_set_style_opa(r.row, LV_OPA_TRANSP, 0);

    return r;
}

/* Build one synthetic departure JSON object and run it through the exact
 * same parse_departure_info() the real path uses — so the placeholder can't
 * drift from production parsing/derivation logic (effective_ts, cancelled)
 * the way the old hand-set struct once did (ADR 0002). */
static DepartureInfo make_dummy_departure(int offset_min, int delay_min,
                                           const char *train, const char *destination,
                                           char cancelled) {
    time_t now = time(NULL);
    time_t planned_ts = now + offset_min * 60;
    time_t changed_ts = delay_min ? planned_ts + delay_min * 60 : 0;

    char pt_str[6];
    struct tm *t = localtime(&planned_ts);
    snprintf(pt_str, sizeof(pt_str), "%02d:%02d", t->tm_hour, t->tm_min);

    cJSON *dep = cJSON_CreateObject();
    cJSON_AddStringToObject(dep, DEP_PLANNED_TIME, pt_str);
    cJSON_AddNumberToObject(dep, DEP_PLANNED_TS,   (double)planned_ts);
    cJSON_AddNumberToObject(dep, DEP_CHANGED_TS,   (double)changed_ts);
    cJSON_AddStringToObject(dep, DEP_TRAIN,        train);
    cJSON_AddStringToObject(dep, DEP_DESTINATION,  destination);
    cJSON_AddStringToObject(dep, DEP_STATUS,       cancelled ? "c" : "p");
    cJSON_AddNumberToObject(dep, DEP_DELAY_MIN,    delay_min);

    DepartureInfo info = parse_departure_info(dep, CFG_I(DELAY_THRESHOLD_MIN));
    cJSON_Delete(dep);
    return info;
}

static void init_dummy_data(void) {
    static const struct {
        int offset_min;   /* minutes from "now" the train is planned */
        int delay_min;    /* 0 = on time */
        const char *train;
        const char *dest;
        char cancelled;
    } d[] = {
        {3,  0, "S1", "Hauptbahnhof",  0},
        {7,  0, "S2", "Flughafen",     0},
        {19, 7, "RE1", "Altstadt",     0},
        {25, 0, "S1", "Messe",         0},
        {33, 0, "S2", "Hauptbahnhof",  1},
        {40, 0, "RB5", "Flughafen",     0},
        {46, 2, "S1", "Westkreuz",     0},  /* below threshold: shown on time */
        {54, 0, "S2", "Ostbahnhof",    0},
    };
    int n = (int)(sizeof(d) / sizeof(d[0]));
    if (n > g_max_rows) n = g_max_rows;

    for (int i = 0; i < n; i++) {
        DepartureInfo info = make_dummy_departure(d[i].offset_min, d[i].delay_min,
                                                   d[i].train, d[i].dest, d[i].cancelled);
        snprintf(info.planned_platform, sizeof(info.planned_platform), "%d", 1 + i % 4);
        if (i == 2) snprintf(info.changed_platform, sizeof(info.changed_platform), "5");
        departure_row_render(&g_rows[i], info);
    }
    show_rows(n);

}

/* Outer flex-column for temp/uv groups: children are horizontally centered */
static lv_obj_t *make_wea_outer(lv_obj_t *parent) {
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(c, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_set_style_pad_row(c, 2, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, 0, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(c, LV_SCROLLBAR_MODE_OFF);
    return c;
}

/* Transparent flex-row for placing gray arrows + white numbers side by side */
static lv_obj_t *make_wea_subrow(lv_obj_t *parent) {
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_set_size(r, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(r, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_set_style_pad_column(r, 3, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_radius(r, 0, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(r, LV_SCROLLBAR_MODE_OFF);
    return r;
}

/* Build one bar group (emoji + fixed-width top value) in `parent` and return
 * the top value label. If sub_out is non-NULL a sub-row is created for
 * make_wea_subslot() calls and returned through it. */
static lv_obj_t *make_wea_group(lv_obj_t *parent, const char *emoji, int top_width,
                                lv_obj_t **sub_out) {
    lv_obj_t *outer = make_wea_outer(parent);
    lv_obj_t *top = make_wea_subrow(outer);
    make_label(top, emoji, font_emoji_wea ? font_emoji_wea : font_hdr, C_TEXT);
    lv_obj_t *val = make_value_label(top, "\xe2\x80\x93", font_hdr, C_TEXT, top_width);
    if (sub_out) *sub_out = make_wea_subrow(outer);
    return val;
}

/* One sub-row slot: optional dim arrow glyph + fixed-width value label. */
static lv_obj_t *make_wea_subslot(lv_obj_t *sub, const char *arrow, int width) {
    if (arrow && *arrow) make_label(sub, arrow, font_col, C_DIM);
    return make_value_label(sub, "\xe2\x80\x93", font_col, C_TEXT, width);
}

static const char *json_str(const cJSON *obj, const char *key, const char *dflt) {
    const char *s = cJSON_GetStringValue(cJSON_GetObjectItem(obj, key));
    return s ? s : dflt;
}

static int json_int(const cJSON *obj, const char *key, int dflt) {
    const cJSON *n = cJSON_GetObjectItem(obj, key);
    return cJSON_IsNumber(n) ? (int)n->valuedouble : dflt;
}

/* Big centred emoji (e.g. a weather condition icon) for an "icon" group. */
static lv_obj_t *make_bar_icon(lv_obj_t *parent, lv_obj_t **outer_out) {
    lv_obj_t *wrap = lv_obj_create(parent);
    lv_obj_remove_style_all(wrap);
    lv_obj_set_size(wrap, 180, LV_PCT(100));
    lv_obj_set_layout(wrap, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(wrap, 40, 0);  /* nudge the emoji right of center */
    lv_obj_set_scrollbar_mode(wrap, LV_SCROLLBAR_MODE_OFF);
    lv_font_t *f = font_emoji_icon ? font_emoji_icon : (font_emoji_hdr ? font_emoji_hdr : font_hdr);
    *outer_out = wrap;
    return make_label(wrap, "", f, C_TEXT);
}

/* Render the combined "groups" array of all plugin files. The widgets are
 * rebuilt only when the layout (kind/emoji/arrows/widths/alignment/counts)
 * changes; otherwise just texts and opacity are updated. */
static void update_bar(const cJSON *groups) {
    int n = cJSON_IsArray(groups) ? cJSON_GetArraySize(groups) : 0;
    if (n > BAR_MAX_GROUPS) n = BAR_MAX_GROUPS;

    char sig[sizeof(g_bar_sig)] = "";
    size_t len = 0;
    for (int g = 0; g < n && len < sizeof(sig); g++) {
        const cJSON *grp = cJSON_GetArrayItem(groups, g);
        len += snprintf(sig + len, sizeof(sig) - len, "%s|%s|%s|%d|",
                        json_str(grp, BAR_ALIGN, "left"),
                        cJSON_GetObjectItem(grp, BAR_BIG_ICON) ? "icon" : "val",
                        json_str(grp, BAR_EMOJI, ""), json_int(grp, BAR_WIDTH, 240));
        const cJSON *subs = cJSON_GetObjectItem(grp, BAR_SUB);
        int ns = cJSON_IsArray(subs) ? cJSON_GetArraySize(subs) : 0;
        if (ns > BAR_MAX_SUB) ns = BAR_MAX_SUB;
        for (int i = 0; i < ns && len < sizeof(sig); i++) {
            const cJSON *sl = cJSON_GetArrayItem(subs, i);
            len += snprintf(sig + len, sizeof(sig) - len, "%s,%d;",
                            json_str(sl, BAR_ARROW, ""), json_int(sl, BAR_WIDTH, 160));
        }
        if (len < sizeof(sig)) len += snprintf(sig + len, sizeof(sig) - len, "/");
    }

    if (n != g_bar_n || strcmp(sig, g_bar_sig) != 0) {
        lv_obj_clean(g_bar_left);
        lv_obj_clean(g_bar_right);
        memset(g_bar, 0, sizeof(g_bar));
        for (int g = 0; g < n; g++) {
            const cJSON *grp = cJSON_GetArrayItem(groups, g);
            lv_obj_t *box = strcmp(json_str(grp, BAR_ALIGN, "left"), "right") == 0
                          ? g_bar_right : g_bar_left;
            if (cJSON_GetObjectItem(grp, BAR_BIG_ICON)) {
                g_bar[g].top = make_bar_icon(box, &g_bar[g].outer);
                continue;
            }
            const cJSON *subs = cJSON_GetObjectItem(grp, BAR_SUB);
            int ns = cJSON_IsArray(subs) ? cJSON_GetArraySize(subs) : 0;
            if (ns > BAR_MAX_SUB) ns = BAR_MAX_SUB;
            lv_obj_t *sub = NULL;
            g_bar[g].top = make_wea_group(box, json_str(grp, BAR_EMOJI, ""),
                                          json_int(grp, BAR_WIDTH, 240), ns ? &sub : NULL);
            g_bar[g].outer = lv_obj_get_parent(lv_obj_get_parent(g_bar[g].top));
            for (int i = 0; i < ns; i++) {
                const cJSON *sl = cJSON_GetArrayItem(subs, i);
                g_bar[g].sub[i] = make_wea_subslot(sub, json_str(sl, BAR_ARROW, NULL),
                                                   json_int(sl, BAR_WIDTH, 160));
            }
            g_bar[g].nsub = ns;
        }
        g_bar_n = n;
        snprintf(g_bar_sig, sizeof(g_bar_sig), "%s", sig);
    }

    for (int g = 0; g < n; g++) {
        const cJSON *grp = cJSON_GetArrayItem(groups, g);
        lv_obj_set_style_opa(g_bar[g].outer,
                             cJSON_IsTrue(cJSON_GetObjectItem(grp, BAR_DIM)) ? LV_OPA_40 : LV_OPA_COVER, 0);
        const char *big = cJSON_GetStringValue(cJSON_GetObjectItem(grp, BAR_BIG_ICON));
        lv_label_set_text(g_bar[g].top, big ? big : json_str(grp, BAR_VALUE, "\xe2\x80\x93"));
        const cJSON *subs = cJSON_GetObjectItem(grp, BAR_SUB);
        for (int i = 0; i < g_bar[g].nsub; i++)
            lv_label_set_text(g_bar[g].sub[i],
                              json_str(cJSON_GetArrayItem(subs, i), BAR_VALUE, "\xe2\x80\x93"));
    }
}

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Scan BAR_DIR; when any file was added/removed/changed, read all of them
 * (sorted by name) and render their concatenated groups. */
static void bar_poll_cb(lv_timer_t *t) {
    (void)t;
    char *names[BAR_MAX_FILES];
    int nf = 0;
    DIR *d = opendir(BAR_DIR);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) && nf < BAR_MAX_FILES) {
            size_t l = strlen(e->d_name);
            if (e->d_name[0] == '.' || l < 6 || strcmp(e->d_name + l - 5, ".json") != 0) continue;
            names[nf++] = strdup(e->d_name);
        }
        closedir(d);
    }
    qsort(names, nf, sizeof(names[0]), cmp_str);

    char files[sizeof(g_bar_files)] = "";
    size_t len = 0;
    char path[512];
    for (int i = 0; i < nf && len < sizeof(files); i++) {
        snprintf(path, sizeof(path), "%s/%s", BAR_DIR, names[i]);
        len += snprintf(files + len, sizeof(files) - len, "%s:%ld;",
                        names[i], (long)file_mtime(path));
    }

    if (g_bar_n < 0 || strcmp(files, g_bar_files) != 0) {
        snprintf(g_bar_files, sizeof(g_bar_files), "%s", files);
        cJSON *all = cJSON_CreateArray();
        for (int i = 0; i < nf; i++) {
            snprintf(path, sizeof(path), "%s/%s", BAR_DIR, names[i]);
            char *raw = read_file(path);
            cJSON *root = raw ? cJSON_Parse(raw) : NULL;
            free(raw);
            const cJSON *groups = root ? cJSON_GetObjectItem(root, BAR_GROUPS) : NULL;
            const cJSON *grp;
            cJSON_ArrayForEach(grp, groups)
                cJSON_AddItemToArray(all, cJSON_Duplicate(grp, 1));
            cJSON_Delete(root);
        }
        update_bar(all);
        cJSON_Delete(all);
    }
    for (int i = 0; i < nf; i++) free(names[i]);
}

void display_init(void) {
    /* FreeType init: cache up to 512 glyphs */
    lv_freetype_init(512);

    font_hdr      = lv_freetype_font_create(CFG_S(FONT_PATH),      LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 72, LV_FREETYPE_FONT_STYLE_NORMAL);
    font_hdr_bold = lv_freetype_font_create(CFG_S(FONT_BOLD_PATH), LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 72, LV_FREETYPE_FONT_STYLE_NORMAL);
    font_row = lv_freetype_font_create(CFG_S(FONT_PATH), LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 68, LV_FREETYPE_FONT_STYLE_NORMAL);
    font_col = lv_freetype_font_create(CFG_S(FONT_PATH), LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 36, LV_FREETYPE_FONT_STYLE_NORMAL);
    font_wea = lv_freetype_font_create(CFG_S(FONT_PATH), LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 64, LV_FREETYPE_FONT_STYLE_NORMAL);

    if (!font_hdr || !font_row || !font_col || !font_wea) {
        fprintf(stderr, "display: failed to load font from %s\n", CFG_S(FONT_PATH));
        font_hdr = font_row = font_col = font_wea = (lv_font_t *)&lv_font_montserrat_16;
    }

    /* Color emoji fallback fonts — one per text size so fallback chain metrics match */
    font_emoji_wea  = lv_freetype_font_create(EMOJI_FONT_PATH, LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 64, LV_FREETYPE_FONT_STYLE_NORMAL);
    font_emoji_hdr  = lv_freetype_font_create(EMOJI_FONT_PATH, LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 72, LV_FREETYPE_FONT_STYLE_NORMAL);
    font_emoji_icon = lv_freetype_font_create(EMOJI_FONT_PATH, LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 130, LV_FREETYPE_FONT_STYLE_NORMAL);
    font_emoji_row  = lv_freetype_font_create(EMOJI_FONT_PATH, LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 68, LV_FREETYPE_FONT_STYLE_NORMAL);
    font_emoji_col  = lv_freetype_font_create(EMOJI_FONT_PATH, LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 36, LV_FREETYPE_FONT_STYLE_NORMAL);

    if (font_emoji_wea) font_wea->fallback = font_emoji_wea;
    if (font_emoji_hdr) font_hdr->fallback = font_emoji_hdr;
    if (font_emoji_row) font_row->fallback = font_emoji_row;
    if (font_emoji_col) font_col->fallback = font_emoji_col;

    if (!font_emoji_wea)
        fprintf(stderr, "display: failed to load color emoji font from %s\n", EMOJI_FONT_PATH);

    lv_obj_t *scr = lv_screen_active();
    set_bg_color(scr, C_BG);
    lv_obj_set_layout(scr, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_pad_row(scr, 0, 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    /* ── header row (absolute positioning — no flex reflow on clock update) */
    lv_obj_t *hdr = lv_obj_create(scr);
    lv_obj_set_width(hdr, LV_PCT(100));
    lv_obj_set_height(hdr, HDR_BAR_H);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_set_scrollbar_mode(hdr, LV_SCROLLBAR_MODE_OFF);
    set_bg_color(hdr, C_HEADER);

    stn_lbl = make_label(hdr, "Null Island", font_hdr_bold ? font_hdr_bold : font_hdr, C_TEXT);
    lv_obj_align(stn_lbl, LV_ALIGN_LEFT_MID, 30, 0);


    /* right-side date+time stack — flex-column, right-aligned */
    lv_obj_t *hdr_right = lv_obj_create(hdr);
    lv_obj_set_size(hdr_right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(hdr_right, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(hdr_right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hdr_right, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_all(hdr_right, 0, 0);
    lv_obj_set_style_pad_row(hdr_right, 0, 0);
    lv_obj_set_style_border_width(hdr_right, 0, 0);
    lv_obj_set_style_radius(hdr_right, 0, 0);
    lv_obj_set_style_bg_opa(hdr_right, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(hdr_right, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(hdr_right, LV_ALIGN_RIGHT_MID, -30, 0);

    date_lbl = make_label(hdr_right, "00.00.0000", font_col, C_DIM);

    /* Time row: three fixed labels so the hours position never shifts */
    lv_obj_t *time_row = lv_obj_create(hdr_right);
    lv_obj_set_size(time_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(time_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(time_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(time_row, 0, 0);
    lv_obj_set_style_pad_column(time_row, 0, 0);
    lv_obj_set_style_border_width(time_row, 0, 0);
    lv_obj_set_style_radius(time_row, 0, 0);
    lv_obj_set_style_bg_opa(time_row, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(time_row, LV_SCROLLBAR_MODE_OFF);
    hours_lbl = make_label(time_row, "00",  font_hdr, C_TEXT);
    colon_lbl = make_label(time_row, ":",   font_hdr, C_TEXT);
    min_lbl   = make_label(time_row, "00",  font_hdr, C_TEXT);

    /* ── departure list container ──────────────────────────────────────── */
    lv_obj_t *list = lv_obj_create(scr);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_layout(list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 0, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_radius(list, 0, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    set_bg_color(list, C_BG);

    /* pre-allocate rows */
    for (int i = 0; i < MAX_ROWS; i++) {
        uint32_t bg = (i % 2 == 0) ? C_BG : 0x21222c;
        g_rows[i] = departure_row_create(list, bg);
    }

    /* ── bottom bar: plugin groups, left box grows, right box hugs the edge ── */
    lv_obj_t *wea_bar = lv_obj_create(scr);
    lv_obj_set_width(wea_bar, LV_PCT(100));
    lv_obj_set_height(wea_bar, BAR_H);
    lv_obj_set_layout(wea_bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(wea_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wea_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(wea_bar, 0, 0);
    lv_obj_set_style_pad_top(wea_bar, 0, 0);
    lv_obj_set_style_pad_left(wea_bar, 30, 0);
    lv_obj_set_style_pad_right(wea_bar, 30, 0);
    lv_obj_set_style_pad_column(wea_bar, 0, 0);
    lv_obj_set_style_border_width(wea_bar, 0, 0);
    lv_obj_set_style_radius(wea_bar, 0, 0);
    lv_obj_set_scrollbar_mode(wea_bar, LV_SCROLLBAR_MODE_OFF);
    set_bg_color(wea_bar, C_BAR);

    for (int side = 0; side < 2; side++) {
        lv_obj_t *box = lv_obj_create(wea_bar);
        lv_obj_remove_style_all(box);
        lv_obj_set_height(box, LV_PCT(100));
        if (side == 0) lv_obj_set_flex_grow(box, 1);
        else           lv_obj_set_width(box, LV_SIZE_CONTENT);
        lv_obj_set_layout(box, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(box, side == 0 ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_END,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(box, 40, 0);
        lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_OFF);
        if (side == 0) g_bar_left = box; else g_bar_right = box;
    }

    /* make one row visible so its height can be measured */
    lv_obj_set_style_opa(g_rows[0].row, LV_OPA_COVER, 0);
    g_row_count = 1;

    /* ── LVGL timers ───────────────────────────────────────────────────── */
    lv_timer_create(clock_cb,        1000,             NULL);
    lv_timer_create(dep_poll_cb,     CFG_I(DEP_POLL_MS),      NULL);
    lv_timer_create(bar_poll_cb,     CFG_I(BAR_POLL_MS),      NULL);
    lv_timer_create(screen_poll_cb,  1000,             NULL);

    /* Compute how many complete rows fit in the list area.
     * Force a layout pass so heights are resolved, then divide available
     * content height by the actual (undecorated) row height. */
    lv_obj_update_layout(scr);
    {
        int32_t avail  = lv_obj_get_content_height(list);
        int32_t row_h  = lv_obj_get_height(g_rows[0].row);
        if (row_h > 0) {
            g_max_rows = (int)(avail / row_h);
            if (g_max_rows > MAX_ROWS) g_max_rows = MAX_ROWS;
            if (g_max_rows < 1)        g_max_rows = 1;
        }
    }

    /* fill dummy data now that g_max_rows is known */
    load_line_colors();
    init_dummy_data();

    /* trigger clock + bar immediately */
    clock_cb(NULL);
    bar_poll_cb(NULL);
}

void display_run(void) {
    for (;;) {
        uint32_t delay = lv_timer_handler();
        if (delay > 16) delay = 16;
        usleep(delay * 1000);
    }
}
