#include "display.h"
#include "gfx.h"
#include "lights.h"
#include "time_ms.h"
#include "vesc.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

/*
 * Dashboardet tegnes i et fast koordinatsystem på 1280x800 (skærmens opløsning).
 * SDL skalerer det automatisk, hvis vinduet har en anden størrelse.
 *
 * For at spare CPU på Pi'en:
 *  - Alt der aldrig ændrer sig tegnes én gang ind i en baggrunds-tekstur.
 *  - Tal renderes kun om 10 gange i sekundet (tekst-rendering er det dyreste).
 *  - Buer og bjælker glider blødt mod deres mål ved hver frame.
 */

#define W 1280
#define H 800

/* TODO: Skalaer til bjælkerne - ret til bilens data */
#define SPEED_MAX_KMH   100.0f
#define VOLTAGE_MIN_V   40.0f
#define VOLTAGE_MAX_V   58.8f
#define CURRENT_MAX_A   100.0f
#define POWER_MAX_W     3000.0f

#define NO_DATA         "NULL"   /* Vises indtil der er kommet et tal ind */
#define TEXT_REFRESH_MS 100
#define SMOOTHING       0.2f     /* Andel af afstanden til målet pr. frame */

/* ─── Farver ─────────────────────────────────── */
static const SDL_Color C_BG          = {  10,  14,  18, 255 };
static const SDL_Color C_BAR_BG      = {  13,  18,  23, 255 };
static const SDL_Color C_CARD        = {  17,  23,  30, 255 };
static const SDL_Color C_BORDER      = {  32,  42,  52, 255 };
static const SDL_Color C_DIVIDER     = {  26,  34,  42, 255 };
static const SDL_Color C_TRACK       = {  30,  40,  48, 255 };
static const SDL_Color C_RING        = {  26,  36,  44, 255 };
static const SDL_Color C_TEXT        = { 232, 238, 242, 255 };
static const SDL_Color C_TITLE       = { 190, 200, 208, 255 };
static const SDL_Color C_MUTED       = { 125, 138, 150, 255 };
static const SDL_Color C_DIM         = {  70,  82,  94, 255 };
static const SDL_Color C_TEAL        = {  78, 230, 184, 255 };
static const SDL_Color C_TEAL_DARK   = {  26, 110,  90, 255 };
static const SDL_Color C_TEAL_BG     = {  22,  58,  50, 255 };
static const SDL_Color C_BLUE        = {  79, 179, 240, 255 };
static const SDL_Color C_BLUE_BG     = {  20,  45,  62, 255 };
static const SDL_Color C_AMBER       = { 255, 176,  32, 255 };
static const SDL_Color C_AMBER_BG    = {  62,  44,  12, 255 };
static const SDL_Color C_RED         = { 240,  72,  72, 255 };
static const SDL_Color C_RED_BG      = {  64,  22,  24, 255 };

static SDL_Color with_alpha(SDL_Color c, Uint8 a)
{
    c.a = a;
    return c;
}

/* ─── Layout ─────────────────────────────────── */
#define TOPBAR_H     84
#define BOTTOMBAR_Y  664
#define COL_W        294
#define LEFT_X       36
#define RIGHT_X      (W - 36 - COL_W)
#define CARD_R       14
#define LOGO_H       58

#define GAUGE_CX     640
#define GAUGE_CY     372
#define GAUGE_R_OUT  214
#define GAUGE_R_IN   196
#define GAUGE_A0     225.0f   /* 0 km/h - nederst til venstre */
#define GAUGE_SWEEP  270.0f   /* Buen går med uret til nederst til højre */

#define TILE_W       104
#define TILE_H       78
#define TILE_Y       692

typedef struct { float x, y, w, h; } box_t;

/* Kort: venstre kolonne */
static const box_t card_voltage = { LEFT_X, 140, COL_W, 150 };
static const box_t card_current = { LEFT_X, 306, COL_W, 150 };
static const box_t card_temps   = { LEFT_X, 472, COL_W, 166 };
/* Kort: højre kolonne */
static const box_t card_battery = { RIGHT_X, 100, COL_W, 150 };
static const box_t card_power   = { RIGHT_X, 266, COL_W, 150 };
static const box_t card_balance = { RIGHT_X, 432, COL_W, 206 };

/* Indikatorer i bunden */
typedef enum { ICON_ARROW_LEFT, ICON_ARROW_RIGHT, ICON_HEADLIGHT, ICON_HAZARD, ICON_BRAKE } icon_t;

typedef struct {
    const char *label;
    icon_t icon;
    float x;
    SDL_Color on, on_bg;
} tile_t;

enum { TILE_LEFT, TILE_RUNNING, TILE_HAZARD, TILE_BRAKE, TILE_RIGHT, TILE_COUNT };

static const tile_t tiles[TILE_COUNT] = {
    [TILE_LEFT]    = { "VENSTRE BLINK", ICON_ARROW_LEFT,  36,                  C_TEAL,  C_TEAL_BG  },
    [TILE_RUNNING] = { "KØRELYS",       ICON_HEADLIGHT,   152,                 C_BLUE,  C_BLUE_BG  },
    [TILE_HAZARD]  = { "HAVARI",        ICON_HAZARD,      (W - TILE_W) / 2.0f, C_AMBER, C_AMBER_BG },
    [TILE_BRAKE]   = { "BREMSE",        ICON_BRAKE,       W - 36 - 2 * TILE_W - 12, C_RED, C_RED_BG },
    [TILE_RIGHT]   = { "HØJRE BLINK",   ICON_ARROW_RIGHT, W - 36 - TILE_W,     C_TEAL,  C_TEAL_BG  },
};

/* ─── Fonte ──────────────────────────────────── */
typedef struct {
    TTF_Font **font;
    const char *file;
    int size;
} font_def_t;

static TTF_Font *f_title, *f_small, *f_caps, *f_header, *f_value, *f_unit, *f_speed,
                *f_speed_unit, *f_tick, *f_time, *f_date, *f_tile, *f_stat, *f_temp;

static const font_def_t font_defs[] = {
    { &f_title,      "Inter-Medium.ttf",         15 },
    { &f_small,      "Inter-Regular.ttf",        12 },
    { &f_caps,       "Inter-SemiBold.ttf",       11 },
    { &f_header,     "Inter-SemiBold.ttf",       14 },
    { &f_tile,       "Inter-SemiBold.ttf",       11 },
    { &f_speed,      "Inter-SemiBold.ttf",      132 },
    { &f_speed_unit, "Inter-Regular.ttf",        18 },
    { &f_date,       "Inter-Medium.ttf",         12 },
    { &f_value,      "JetBrainsMono-Regular.ttf", 44 },
    { &f_unit,       "JetBrainsMono-Medium.ttf", 16 },
    { &f_temp,       "JetBrainsMono-Regular.ttf", 30 },
    { &f_tick,       "JetBrainsMono-Regular.ttf", 12 },
    { &f_stat,       "JetBrainsMono-Medium.ttf", 18 },
    { &f_time,       "JetBrainsMono-Bold.ttf",   26 },
};

/* ─── Tilstand ───────────────────────────────── */
static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *background;
static SDL_Texture *logo;
static SDL_FRect logo_rect;

/* Cachede tekster - renderes kun om når indholdet ændrer sig */
enum {
    T_TIME, T_DATE, T_STATUS,
    T_VOLT, T_VOLT_UNIT, T_VOLT_SUB,
    T_CURR, T_CURR_UNIT, T_CURR_SUB,
    T_TEMP_FET, T_TEMP_MOTOR,
    T_BATT, T_BATT_UNIT, T_BATT_SUB,
    T_POWER, T_POWER_UNIT, T_POWER_SUB,
    T_MODE, T_WH_USED, T_WH_REGEN,
    T_SPEED, T_TRIP, T_EFFICIENCY,
    T_COUNT
};
static gfx_text_t texts[T_COUNT];
static gfx_text_t tile_texts[TILE_COUNT];

/* Strenge der vises - opdateres 10 gange i sekundet */
static char s_time[16], s_date[24];
static char s_volt[16], s_volt_sub[40];
static char s_curr[16], s_curr_sub[40];
static char s_temp_fet[16], s_temp_motor[16];
static char s_batt[16], s_batt_sub[40];
static char s_power[16], s_power_unit[8], s_power_sub[40];
static char s_wh_used[24], s_wh_regen[24], s_mode[8];
static char s_speed[8], s_trip[16], s_efficiency[16];
static bool vesc_online;
static unsigned int last_text_ms;

/* Blødt animerede værdier (0-1 for bjælker, km/h for hastighed) */
static float anim_speed, anim_volt, anim_curr, anim_batt, anim_power, anim_balance;
static float target_speed, target_volt, target_curr, target_batt, target_power, target_balance;

/* ─── Hjælpere ───────────────────────────────── */

static float clamp01(float v)
{
    return fminf(fmaxf(v, 0.0f), 1.0f);
}

static void asset_path(char *out, size_t size, const char *rel)
{
    /* Stier er relative til mappen med den eksekverbare fil (build/) */
    char *base = SDL_GetBasePath();
    snprintf(out, size, "%s../assets/%s", base ? base : "./", rel);
    SDL_free(base);
}

static void draw_card(const box_t *b)
{
    gfx_fill_rounded(b->x, b->y, b->w, b->h, CARD_R, C_CARD);
    gfx_stroke_rounded(b->x, b->y, b->w, b->h, CARD_R, C_BORDER);
}

static void draw_bar(float x, float y, float w, float frac, SDL_Color c)
{
    gfx_fill_rounded(x, y, w, 4, 2, C_TRACK);
    if (frac > 0.005f)
        gfx_fill_rounded(x, y, fmaxf(w * clamp01(frac), 4), 4, 2, c);
}

static void draw_pill(float x, float y, const char *text, gfx_text_t *t,
                      SDL_Color fg, SDL_Color bg, gfx_align_t align)
{
    /* Mål teksten først, så pillen kan få den rigtige bredde */
    int tw = 0, th = 0;
    TTF_SizeUTF8(f_caps, text, &tw, &th);
    float w = tw + 40, h = 28;
    if (align == ALIGN_RIGHT)
        x -= w;
    else if (align == ALIGN_CENTER)
        x -= w / 2;

    gfx_fill_rounded(x, y, w, h, h / 2, bg);
    gfx_fill_circle(x + 16, y + h / 2, 3.5f, fg);
    if (t)
        gfx_text(t, f_caps, fg, text, x + 26, y + (h - th) / 2, ALIGN_LEFT);
    else
        gfx_text_once(f_caps, fg, text, x + 26, y + (h - th) / 2, ALIGN_LEFT);
}

/* ─── Ikoner (tegnet med streger, så de skalerer pænt) ── */

static void draw_icon(icon_t icon, float cx, float cy, SDL_Color c)
{
    const float lw = 2.2f;

    switch (icon) {
    case ICON_ARROW_LEFT:
    case ICON_ARROW_RIGHT: {
        float d = (icon == ICON_ARROW_LEFT) ? -1.0f : 1.0f;
        gfx_line(cx - 11 * d, cy, cx + 11 * d, cy, lw, c);
        gfx_line(cx + 11 * d, cy, cx + 3 * d, cy - 8, lw, c);
        gfx_line(cx + 11 * d, cy, cx + 3 * d, cy + 8, lw, c);
        break;
    }
    case ICON_HEADLIGHT: {
        /* "D"-formet lygte med lysstråler til venstre */
        float lx = cx + 1;
        for (int i = 0; i < 8; i++) {
            float a0 = (-90 + 22.5f * i) * (float)M_PI / 180, a1 = (-90 + 22.5f * (i + 1)) * (float)M_PI / 180;
            gfx_line(lx + 9 * cosf(a0), cy + 9 * sinf(a0), lx + 9 * cosf(a1), cy + 9 * sinf(a1), lw, c);
        }
        gfx_line(lx, cy - 9, lx, cy + 9, lw, c);
        for (int i = -1; i <= 1; i++)
            gfx_line(cx - 14, cy + i * 6, cx - 5, cy + i * 6, lw, c);
        break;
    }
    case ICON_HAZARD:
        gfx_line(cx, cy - 11, cx + 12, cy + 9, lw, c);
        gfx_line(cx + 12, cy + 9, cx - 12, cy + 9, lw, c);
        gfx_line(cx - 12, cy + 9, cx, cy - 11, lw, c);
        gfx_line(cx, cy - 4, cx + 5, cy + 5, lw, c);
        gfx_line(cx + 5, cy + 5, cx - 5, cy + 5, lw, c);
        gfx_line(cx - 5, cy + 5, cx, cy - 4, lw, c);
        break;
    case ICON_BRAKE:
        gfx_arc(cx, cy, 8, 10, 0, 360, c, c);
        gfx_line(cx, cy - 5, cx, cy + 1.5f, lw, c);
        gfx_fill_circle(cx, cy + 4.5f, 1.3f, c);
        break;
    }
}

/* Små ikoner i kortenes hjørner */
static void draw_card_icon(const box_t *b, SDL_Color fg, SDL_Color bg, int kind)
{
    float x = b->x + b->w - 18 - 30, y = b->y + 16;
    float cx = x + 15, cy = y + 15;
    const float lw = 1.8f;

    gfx_fill_rounded(x, y, 30, 30, 7, bg);
    switch (kind) {
    case 0: /* Batteri */
        gfx_line(cx - 8, cy - 5, cx + 6, cy - 5, lw, fg);
        gfx_line(cx + 6, cy - 5, cx + 6, cy + 5, lw, fg);
        gfx_line(cx + 6, cy + 5, cx - 8, cy + 5, lw, fg);
        gfx_line(cx - 8, cy + 5, cx - 8, cy - 5, lw, fg);
        gfx_line(cx + 8.5f, cy - 2, cx + 8.5f, cy + 2, lw, fg);
        gfx_fill_rounded(cx - 5.5f, cy - 2.5f, 7, 5, 1, fg);
        break;
    case 1: /* Puls */
        gfx_line(cx - 9, cy, cx - 4, cy, lw, fg);
        gfx_line(cx - 4, cy, cx - 1, cy - 7, lw, fg);
        gfx_line(cx - 1, cy - 7, cx + 2, cy + 7, lw, fg);
        gfx_line(cx + 2, cy + 7, cx + 5, cy, lw, fg);
        gfx_line(cx + 5, cy, cx + 9, cy, lw, fg);
        break;
    case 2: /* Lyn */
        gfx_line(cx + 3, cy - 9, cx - 5, cy + 1, lw, fg);
        gfx_line(cx - 5, cy + 1, cx + 5, cy - 1, lw, fg);
        gfx_line(cx + 5, cy - 1, cx - 3, cy + 9, lw, fg);
        break;
    case 3: /* Termometer */
        gfx_line(cx, cy - 8, cx, cy + 3, lw, fg);
        gfx_arc(cx, cy + 5.5f, 2.6f, 4.4f, 0, 360, fg, fg);
        gfx_line(cx + 3, cy - 5, cx + 6, cy - 5, 1.4f, fg);
        gfx_line(cx + 3, cy - 1, cx + 6, cy - 1, 1.4f, fg);
        break;
    }
}

static float speed_angle(float kmh)
{
    return GAUGE_A0 - GAUGE_SWEEP * clamp01(kmh / SPEED_MAX_KMH);
}

/* ─── Statisk baggrund (tegnes én gang) ──────── */

static void draw_static(void)
{
    SDL_SetRenderDrawColor(renderer, C_BG.r, C_BG.g, C_BG.b, 255);
    SDL_RenderClear(renderer);

    /* Top- og bundbjælke */
    SDL_SetRenderDrawColor(renderer, C_BAR_BG.r, C_BAR_BG.g, C_BAR_BG.b, 255);
    SDL_RenderFillRect(renderer, &(SDL_Rect){ 0, 0, W, TOPBAR_H });
    SDL_RenderFillRect(renderer, &(SDL_Rect){ 0, BOTTOMBAR_Y, W, H - BOTTOMBAR_Y });
    SDL_SetRenderDrawColor(renderer, C_DIVIDER.r, C_DIVIDER.g, C_DIVIDER.b, 255);
    SDL_RenderDrawLine(renderer, 0, TOPBAR_H, W, TOPBAR_H);
    SDL_RenderDrawLine(renderer, 0, BOTTOMBAR_Y, W, BOTTOMBAR_Y);

    if (logo)
        SDL_RenderCopyF(renderer, logo, NULL, &logo_rect);

    /* Venstre kolonne */
    gfx_text_once(f_header, C_TITLE, "ELEKTRISK SYSTEM", LEFT_X + 2, 107, ALIGN_LEFT);

    draw_card(&card_voltage);
    gfx_text_once(f_title, C_TITLE, "Battery voltage", card_voltage.x + 18, card_voltage.y + 22, ALIGN_LEFT);
    draw_card_icon(&card_voltage, C_TEAL, C_TEAL_BG, 0);

    draw_card(&card_current);
    gfx_text_once(f_title, C_TITLE, "Battery current", card_current.x + 18, card_current.y + 22, ALIGN_LEFT);
    draw_card_icon(&card_current, C_BLUE, C_BLUE_BG, 1);

    draw_card(&card_temps);
    gfx_text_once(f_header, C_TITLE, "DRIVLINJE", card_temps.x + 18, card_temps.y + 22, ALIGN_LEFT);
    draw_card_icon(&card_temps, C_MUTED, C_TRACK, 3);
    gfx_text_once(f_caps, C_MUTED, "CONTROLLER", card_temps.x + 18, card_temps.y + 124, ALIGN_LEFT);
    gfx_text_once(f_caps, C_MUTED, "MOTOR", card_temps.x + card_temps.w - 18, card_temps.y + 124, ALIGN_RIGHT);

    /* Højre kolonne */
    draw_card(&card_battery);
    gfx_text_once(f_title, C_TITLE, "Battery capacity", card_battery.x + 18, card_battery.y + 22, ALIGN_LEFT);
    draw_card_icon(&card_battery, C_TEAL, C_TEAL_BG, 0);

    draw_card(&card_power);
    gfx_text_once(f_title, C_TITLE, "Watt output", card_power.x + 18, card_power.y + 22, ALIGN_LEFT);
    draw_card_icon(&card_power, C_BLUE, C_BLUE_BG, 2);

    draw_card(&card_balance);
    gfx_text_once(f_header, C_TITLE, "EFFEKTBALANCE", card_balance.x + 18, card_balance.y + 22, ALIGN_LEFT);
    gfx_text_once(f_caps, C_MUTED, "CHARGE", card_balance.x + 18, card_balance.y + 78, ALIGN_LEFT);
    gfx_text_once(f_caps, C_MUTED, "POWER", card_balance.x + card_balance.w - 18, card_balance.y + 78, ALIGN_RIGHT);
    SDL_SetRenderDrawColor(renderer, C_DIVIDER.r, C_DIVIDER.g, C_DIVIDER.b, 255);
    SDL_RenderDrawLine(renderer, card_balance.x + 18, card_balance.y + 108,
                       card_balance.x + card_balance.w - 18, card_balance.y + 108);
    gfx_text_once(f_small, C_MUTED, "Energi brugt", card_balance.x + 18, card_balance.y + 126, ALIGN_LEFT);
    gfx_text_once(f_small, C_MUTED, "Energi genvundet", card_balance.x + 18, card_balance.y + 160, ALIGN_LEFT);

    /* Speedometer: ringe, spor og skala */
    gfx_stroke_circle(GAUGE_CX, GAUGE_CY, GAUGE_R_OUT + 24, C_RING);
    gfx_stroke_circle(GAUGE_CX, GAUGE_CY, GAUGE_R_IN - 36, C_RING);
    gfx_arc(GAUGE_CX, GAUGE_CY, GAUGE_R_IN, GAUGE_R_OUT, GAUGE_A0, GAUGE_A0 - GAUGE_SWEEP, C_TRACK, C_TRACK);

    for (int v = 0; v <= (int)SPEED_MAX_KMH; v += 10) {
        float a = speed_angle((float)v) * (float)M_PI / 180;
        float ca = cosf(a), sa = -sinf(a);
        bool major = v % 20 == 0;
        float r0 = GAUGE_R_OUT + 6, r1 = GAUGE_R_OUT + (major ? 16 : 11);
        gfx_line(GAUGE_CX + r0 * ca, GAUGE_CY + r0 * sa, GAUGE_CX + r1 * ca, GAUGE_CY + r1 * sa,
                 major ? 2.0f : 1.2f, major ? C_MUTED : C_DIM);
        if (major) {
            char label[8];
            snprintf(label, sizeof label, "%d", v);
            float lr = GAUGE_R_IN - 18;
            gfx_text_once(f_tick, C_MUTED, label, GAUGE_CX + lr * ca, GAUGE_CY + lr * sa - 8, ALIGN_CENTER);
        }
    }

    draw_pill(GAUGE_CX, 96, "HASTIGHED", NULL, C_TEAL, C_TEAL_BG, ALIGN_CENTER);
    gfx_text_once(f_speed_unit, C_MUTED, "km/t", GAUGE_CX, GAUGE_CY + 62, ALIGN_CENTER);
    gfx_text_once(f_caps, C_MUTED, "TRIP", GAUGE_CX - 70, 636, ALIGN_CENTER);
    gfx_text_once(f_caps, C_MUTED, "Wh/km", GAUGE_CX + 70, 636, ALIGN_CENTER);
}

/* ─── Data → tekst (10 gange i sekundet) ─────── */

static const char *DAYS[]   = { "SØN", "MAN", "TIR", "ONS", "TOR", "FRE", "LØR" };
static const char *MONTHS[] = { "JAN", "FEB", "MAR", "APR", "MAJ", "JUN",
                                "JUL", "AUG", "SEP", "OKT", "NOV", "DEC" };

static void update_texts(unsigned int now)
{
    const vesc_data_t *v = vesc_get();
    bool s1 = vesc_fresh(VESC_STATUS_1, now);
    bool s2 = vesc_fresh(VESC_STATUS_2, now);
    bool s3 = vesc_fresh(VESC_STATUS_3, now);
    bool s4 = vesc_fresh(VESC_STATUS_4, now);
    bool s5 = vesc_fresh(VESC_STATUS_5, now);

    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    snprintf(s_time, sizeof s_time, "%02d:%02d", tm.tm_hour, tm.tm_min);
    snprintf(s_date, sizeof s_date, "%s %d %s", DAYS[tm.tm_wday], tm.tm_mday, MONTHS[tm.tm_mon]);

    vesc_online = vesc_is_alive(now);

    /* Spænding */
    if (s5) {
        snprintf(s_volt, sizeof s_volt, "%.1f", v->voltage_in);
        snprintf(s_volt_sub, sizeof s_volt_sub, "Område %.0f–%.0f V", VOLTAGE_MIN_V, VOLTAGE_MAX_V);
        target_volt = clamp01((v->voltage_in - VOLTAGE_MIN_V) / (VOLTAGE_MAX_V - VOLTAGE_MIN_V));
    } else {
        snprintf(s_volt, sizeof s_volt, NO_DATA);
        snprintf(s_volt_sub, sizeof s_volt_sub, "Venter på VESC");
        target_volt = 0;
    }

    /* Strøm */
    if (s4) {
        snprintf(s_curr, sizeof s_curr, "%.1f", v->current_in);
        snprintf(s_curr_sub, sizeof s_curr_sub, "%s",
                 v->current_in < -0.5f ? "Regenerativ opladning" :
                 v->current_in > 0.5f  ? "Afladning" : "Hvile");
        target_curr = clamp01(fabsf(v->current_in) / CURRENT_MAX_A);
        snprintf(s_temp_fet, sizeof s_temp_fet, "%.0f°", v->temp_fet);
        snprintf(s_temp_motor, sizeof s_temp_motor, "%.0f°", v->temp_motor);
    } else {
        snprintf(s_curr, sizeof s_curr, NO_DATA);
        snprintf(s_curr_sub, sizeof s_curr_sub, "Venter på VESC");
        target_curr = 0;
        snprintf(s_temp_fet, sizeof s_temp_fet, NO_DATA);
        snprintf(s_temp_motor, sizeof s_temp_motor, NO_DATA);
    }

    /* Batteri */
    if (s2) {
        float pct = vesc_battery_percent();
        snprintf(s_batt, sizeof s_batt, "%.0f", pct);
        snprintf(s_batt_sub, sizeof s_batt_sub, "%.2f Ah brugt af %.0f Ah",
                 v->amp_hours - v->amp_hours_charged, BATTERY_CAPACITY_AH);
        target_batt = pct / 100.0f;
    } else {
        snprintf(s_batt, sizeof s_batt, NO_DATA);
        snprintf(s_batt_sub, sizeof s_batt_sub, "Venter på VESC");
        target_batt = 0;
    }

    /* Effekt */
    if (s4 && s5) {
        float p = vesc_power_w();
        if (fabsf(p) >= 1000) {
            snprintf(s_power, sizeof s_power, "%.2f", p / 1000);
            snprintf(s_power_unit, sizeof s_power_unit, "kW");
        } else {
            snprintf(s_power, sizeof s_power, "%.0f", p);
            snprintf(s_power_unit, sizeof s_power_unit, "W");
        }
        snprintf(s_power_sub, sizeof s_power_sub, "%s",
                 p < -20 ? "Energi tilbage til batteri" :
                 p > 20  ? "Forbrug fra batteri" : "Ingen belastning");
        target_power = clamp01(fabsf(p) / POWER_MAX_W);
        target_balance = fmaxf(-1.0f, fminf(1.0f, p / POWER_MAX_W));
        snprintf(s_mode, sizeof s_mode, "%s", p < -20 ? "REGEN" : p > 20 ? "DRIVE" : "IDLE");
    } else {
        snprintf(s_power, sizeof s_power, NO_DATA);
        snprintf(s_power_unit, sizeof s_power_unit, "W");
        snprintf(s_power_sub, sizeof s_power_sub, "Venter på VESC");
        target_power = 0;
        target_balance = 0;
        snprintf(s_mode, sizeof s_mode, NO_DATA);
    }

    /* Energi */
    if (s3) {
        snprintf(s_wh_used, sizeof s_wh_used, "%.1f Wh", v->watt_hours);
        snprintf(s_wh_regen, sizeof s_wh_regen, "%.1f Wh", v->watt_hours_charged);
    } else {
        snprintf(s_wh_used, sizeof s_wh_used, NO_DATA);
        snprintf(s_wh_regen, sizeof s_wh_regen, NO_DATA);
    }

    /* Hastighed og tur */
    if (s1) {
        target_speed = vesc_speed_kmh();
        snprintf(s_speed, sizeof s_speed, "%.0f", target_speed);
    } else {
        target_speed = 0;
        snprintf(s_speed, sizeof s_speed, NO_DATA);
    }

    if (s5) {
        float km = vesc_trip_km();
        snprintf(s_trip, sizeof s_trip, "%.1f km", km);
        if (s3 && km > 0.05f)
            snprintf(s_efficiency, sizeof s_efficiency, "%.1f", (v->watt_hours - v->watt_hours_charged) / km);
        else
            snprintf(s_efficiency, sizeof s_efficiency, NO_DATA);
    } else {
        snprintf(s_trip, sizeof s_trip, NO_DATA);
        snprintf(s_efficiency, sizeof s_efficiency, NO_DATA);
    }
}

/* ─── Dynamiske dele (hver frame) ────────────── */

static void draw_value(const box_t *b, int t_val, int t_unit, const char *val, const char *unit,
                       SDL_Color unit_color)
{
    int w = gfx_text(&texts[t_val], f_value, C_TEXT, val, b->x + 16, b->y + 50, ALIGN_LEFT);
    gfx_text(&texts[t_unit], f_unit, unit_color, unit, b->x + 16 + w + 8, b->y + 76, ALIGN_LEFT);
}

static void draw_cards(void)
{
    draw_value(&card_voltage, T_VOLT, T_VOLT_UNIT, s_volt, "V", C_TEAL);
    gfx_text(&texts[T_VOLT_SUB], f_small, C_MUTED, s_volt_sub, card_voltage.x + 18, card_voltage.y + 108, ALIGN_LEFT);
    draw_bar(card_voltage.x + 18, card_voltage.y + 132, card_voltage.w - 36, anim_volt, C_TEAL);

    draw_value(&card_current, T_CURR, T_CURR_UNIT, s_curr, "A", C_BLUE);
    gfx_text(&texts[T_CURR_SUB], f_small, C_MUTED, s_curr_sub, card_current.x + 18, card_current.y + 108, ALIGN_LEFT);
    draw_bar(card_current.x + 18, card_current.y + 132, card_current.w - 36, anim_curr, C_BLUE);

    gfx_text(&texts[T_TEMP_FET], f_temp, C_TEXT, s_temp_fet, card_temps.x + 18, card_temps.y + 70, ALIGN_LEFT);
    gfx_text(&texts[T_TEMP_MOTOR], f_temp, C_TEXT, s_temp_motor,
             card_temps.x + card_temps.w - 18, card_temps.y + 70, ALIGN_RIGHT);

    draw_value(&card_battery, T_BATT, T_BATT_UNIT, s_batt, "%", C_TEAL);
    gfx_text(&texts[T_BATT_SUB], f_small, C_MUTED, s_batt_sub, card_battery.x + 18, card_battery.y + 108, ALIGN_LEFT);
    draw_bar(card_battery.x + 18, card_battery.y + 132, card_battery.w - 36, anim_batt, C_TEAL);

    draw_value(&card_power, T_POWER, T_POWER_UNIT, s_power, s_power_unit, C_BLUE);
    gfx_text(&texts[T_POWER_SUB], f_small, C_MUTED, s_power_sub, card_power.x + 18, card_power.y + 108, ALIGN_LEFT);
    draw_bar(card_power.x + 18, card_power.y + 132, card_power.w - 36, anim_power, C_BLUE);

    /* Effektbalance: 12 segmenter, regen fylder mod venstre, forbrug mod højre */
    const box_t *b = &card_balance;
    const int segs = 12;
    float gap = 4, sw = (b->w - 36 - gap * (segs - 1)) / segs;
    int lit = (int)roundf(fabsf(anim_balance) * segs / 2);
    for (int i = 0; i < segs; i++) {
        bool left_half = i < segs / 2;
        int dist = left_half ? segs / 2 - 1 - i : i - segs / 2;   /* 0 = tættest på midten */
        bool on = dist < lit && (left_half ? anim_balance < 0 : anim_balance > 0);
        SDL_Color c = on ? (left_half ? C_BLUE : C_TEAL) : C_TRACK;
        gfx_fill_rounded(b->x + 18 + i * (sw + gap), b->y + 58, sw, 8, 4, c);
    }

    SDL_Color mode_c = s_mode[0] == 'R' ? C_BLUE : s_mode[0] == 'D' ? C_TEAL : C_MUTED;
    gfx_text(&texts[T_MODE], f_caps, mode_c, s_mode, b->x + b->w - 18, b->y + 24, ALIGN_RIGHT);
    gfx_text(&texts[T_WH_USED], f_stat, C_TEXT, s_wh_used, b->x + b->w - 18, b->y + 122, ALIGN_RIGHT);
    gfx_text(&texts[T_WH_REGEN], f_stat, C_BLUE, s_wh_regen, b->x + b->w - 18, b->y + 156, ALIGN_RIGHT);
}

static void draw_gauge(void)
{
    float a_end = speed_angle(anim_speed);

    if (anim_speed > 0.3f) {
        /* Glød: bredere, gennemsigtige buer bag selve buen */
        gfx_arc(GAUGE_CX, GAUGE_CY, GAUGE_R_IN - 10, GAUGE_R_OUT + 10, GAUGE_A0, a_end,
                with_alpha(C_TEAL, 0), with_alpha(C_TEAL, 28));
        gfx_arc(GAUGE_CX, GAUGE_CY, GAUGE_R_IN - 4, GAUGE_R_OUT + 4, GAUGE_A0, a_end,
                with_alpha(C_TEAL, 0), with_alpha(C_TEAL, 50));
        gfx_arc(GAUGE_CX, GAUGE_CY, GAUGE_R_IN, GAUGE_R_OUT, GAUGE_A0, a_end, C_TEAL_DARK, C_TEAL);

        float a = a_end * (float)M_PI / 180, r = (GAUGE_R_IN + GAUGE_R_OUT) / 2.0f;
        float px = GAUGE_CX + r * cosf(a), py = GAUGE_CY - r * sinf(a);
        gfx_fill_circle(px, py, 13, with_alpha(C_TEAL, 60));
        gfx_fill_circle(px, py, 9, C_TEAL);
        gfx_fill_circle(px, py, 3.5f, C_TEXT);
    }

    gfx_text(&texts[T_SPEED], f_speed, C_TEXT, s_speed, GAUGE_CX, GAUGE_CY - 100, ALIGN_CENTER);
    gfx_text(&texts[T_TRIP], f_stat, C_TEXT, s_trip, GAUGE_CX - 70, 612, ALIGN_CENTER);
    gfx_text(&texts[T_EFFICIENCY], f_stat, C_TEXT, s_efficiency, GAUGE_CX + 70, 612, ALIGN_CENTER);
}

static void draw_tiles(unsigned int now)
{
    bool phase = lights_blink_phase(now);
    bool hazard = lights_is_on(LIGHT_HAZARD);
    bool lit[TILE_COUNT] = {
        [TILE_LEFT]    = (lights_is_on(LIGHT_TURN_LEFT) || hazard) && phase,
        [TILE_RUNNING] = lights_is_on(LIGHT_RUNNING),
        [TILE_HAZARD]  = hazard && phase,
        [TILE_BRAKE]   = lights_is_on(LIGHT_BRAKE),
        [TILE_RIGHT]   = (lights_is_on(LIGHT_TURN_RIGHT) || hazard) && phase,
    };

    for (int i = 0; i < TILE_COUNT; i++) {
        const tile_t *t = &tiles[i];
        SDL_Color fg = lit[i] ? t->on : C_MUTED;

        gfx_fill_rounded(t->x, TILE_Y, TILE_W, TILE_H, 12, lit[i] ? t->on_bg : C_CARD);
        gfx_stroke_rounded(t->x, TILE_Y, TILE_W, TILE_H, 12, lit[i] ? t->on : C_BORDER);
        draw_icon(t->icon, t->x + TILE_W / 2, TILE_Y + 28, fg);
        gfx_text(&tile_texts[i], f_tile, lit[i] ? C_TEXT : C_MUTED, t->label,
                 t->x + TILE_W / 2, TILE_Y + 50, ALIGN_CENTER);
    }
}

static void draw_topbar(void)
{
    gfx_text(&texts[T_TIME], f_time, C_TEXT, s_time, W - 36, 14, ALIGN_RIGHT);
    gfx_text(&texts[T_DATE], f_date, C_MUTED, s_date, W - 36, 50, ALIGN_RIGHT);

    draw_pill(LEFT_X + COL_W, 100, vesc_online ? "VESC ONLINE" : "VESC OFFLINE", &texts[T_STATUS],
              vesc_online ? C_TEAL : C_MUTED, vesc_online ? C_TEAL_BG : C_TRACK, ALIGN_RIGHT);
}

/* ─── Offentlige funktioner ──────────────────── */

/*
 * Nedskalering med gennemsnit over alle kildepixels pr. målpixel (box filter).
 * Farverne vægtes med alpha, så gennemsigtige pixels ikke giver mørke kanter.
 */
static SDL_Surface *downscale(SDL_Surface *src, int dw, int dh)
{
    SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, dw, dh, 32, SDL_PIXELFORMAT_RGBA32);
    if (!dst)
        return NULL;

    for (int dy = 0; dy < dh; dy++) {
        int sy0 = dy * src->h / dh, sy1 = SDL_max((dy + 1) * src->h / dh, sy0 + 1);
        Uint8 *out = (Uint8 *)dst->pixels + dy * dst->pitch;

        for (int dx = 0; dx < dw; dx++, out += 4) {
            int sx0 = dx * src->w / dw, sx1 = SDL_max((dx + 1) * src->w / dw, sx0 + 1);
            unsigned long r = 0, g = 0, b = 0, a = 0, n = 0;

            for (int sy = sy0; sy < sy1; sy++) {
                const Uint8 *p = (const Uint8 *)src->pixels + sy * src->pitch + sx0 * 4;
                for (int sx = sx0; sx < sx1; sx++, p += 4) {
                    r += p[0] * p[3];
                    g += p[1] * p[3];
                    b += p[2] * p[3];
                    a += p[3];
                    n++;
                }
            }
            out[0] = a ? (Uint8)(r / a) : 0;
            out[1] = a ? (Uint8)(g / a) : 0;
            out[2] = a ? (Uint8)(b / a) : 0;
            out[3] = (Uint8)(a / n);
        }
    }
    return dst;
}

static void load_logo(void)
{
    char path[512];
    asset_path(path, sizeof path, "logo.png");

    SDL_Surface *loaded = IMG_Load(path);
    if (!loaded) {
        fprintf(stderr, "[skærm] kunne ikke indlæse logo (%s): %s\n", path, IMG_GetError());
        return;
    }
    SDL_Surface *s = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(loaded);
    if (!s)
        return;

    /*
     * Logoets sorte tekst er usynlig på mørk baggrund. Alle grå/sorte pixels (også de
     * halvgennemsigtige kanter) farves lyse; det orange har høj farvemætning og bevares.
     */
    SDL_LockSurface(s);
    for (int y = 0; y < s->h; y++) {
        Uint8 *p = (Uint8 *)s->pixels + y * s->pitch;
        for (int x = 0; x < s->w; x++, p += 4) {
            int mx = SDL_max(p[0], SDL_max(p[1], p[2]));
            int mn = SDL_min(p[0], SDL_min(p[1], p[2]));
            if (p[3] > 0 && mx - mn < 40) {
                p[0] = C_TEXT.r;
                p[1] = C_TEXT.g;
                p[2] = C_TEXT.b;
            }
        }
    }
    SDL_UnlockSurface(s);

    /* Skalér ned én gang i god kvalitet - GPU'ens skalering giver takkede kanter ved så stor forskel */
    int h = LOGO_H;
    int w = s->w * h / s->h;
    SDL_Surface *small = downscale(s, w, h);
    SDL_FreeSurface(s);
    if (!small)
        return;

    logo = SDL_CreateTextureFromSurface(renderer, small);
    logo_rect = (SDL_FRect){ LEFT_X, (TOPBAR_H - h) / 2.0f, (float)w, (float)h };
    SDL_FreeSurface(small);
}

static int load_fonts(void)
{
    for (size_t i = 0; i < SDL_arraysize(font_defs); i++) {
        char path[512];
        asset_path(path, sizeof path, "fonts/");
        SDL_strlcat(path, font_defs[i].file, sizeof path);
        *font_defs[i].font = TTF_OpenFont(path, font_defs[i].size);
        if (!*font_defs[i].font) {
            fprintf(stderr, "[skærm] kunne ikke åbne font %s: %s\n", path, TTF_GetError());
            return -1;
        }
    }
    return 0;
}

int display_init(bool fullscreen)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "[skærm] SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    if (TTF_Init() != 0) {
        fprintf(stderr, "[skærm] TTF_Init: %s\n", TTF_GetError());
        return -1;
    }
    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG))
        fprintf(stderr, "[skærm] IMG_Init: %s\n", IMG_GetError());

    Uint32 flags = fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_RESIZABLE;
    window = SDL_CreateWindow("Solbil", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              W * 3 / 4, H * 3 / 4, flags);
    if (!window) {
        fprintf(stderr, "[skærm] SDL_CreateWindow: %s\n", SDL_GetError());
        return -1;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
    if (!renderer)
        renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) {
        fprintf(stderr, "[skærm] SDL_CreateRenderer: %s\n", SDL_GetError());
        return -1;
    }

    SDL_RendererInfo info;
    SDL_GetRendererInfo(renderer, &info);
    int out_w, out_h;
    SDL_GetRendererOutputSize(renderer, &out_w, &out_h);
    printf("[skærm] %dx%d via %s\n", out_w, out_h, info.name);

    if (fullscreen)
        SDL_ShowCursor(SDL_DISABLE);

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    SDL_RenderSetLogicalSize(renderer, W, H);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    gfx_init(renderer);

    if (load_fonts() != 0)
        return -1;
    load_logo();

    /* Tegn den statiske baggrund én gang ind i en tekstur */
    background = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, W, H);
    if (background && SDL_SetRenderTarget(renderer, background) == 0) {
        draw_static();
        SDL_SetRenderTarget(renderer, NULL);
    } else {
        fprintf(stderr, "[skærm] ingen render-target - tegner baggrund hver frame\n");
        if (background)
            SDL_DestroyTexture(background);
        background = NULL;
    }

    update_texts(time_ms());
    return 0;
}

bool display_handle_events(void)
{
    SDL_Event ev;

    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT)
            return false;
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
            return false;
        /* Baggrunds-teksturen kan gå tabt ved fx skift af skærm */
        if (ev.type == SDL_RENDER_TARGETS_RESET && background) {
            SDL_SetRenderTarget(renderer, background);
            draw_static();
            SDL_SetRenderTarget(renderer, NULL);
        }
    }
    return true;
}

static void approach(float *value, float target)
{
    *value += (target - *value) * SMOOTHING;
}

/*
 * Hvis vinduet ikke har samme form som 1280x800, lægger SDL tomme kanter rundt om
 * dashboardet. Fyld dem med baggrunden og forlæng top- og bundbjælken ud til kanten.
 */
static void fill_borders(void)
{
    int ow, oh;
    SDL_GetRendererOutputSize(renderer, &ow, &oh);
    float scale = fminf((float)ow / W, (float)oh / H);
    float oy = (oh - H * scale) / 2;

    SDL_RenderSetLogicalSize(renderer, 0, 0);   /* Tegn i vinduets egne pixels */

    SDL_SetRenderDrawColor(renderer, C_BG.r, C_BG.g, C_BG.b, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, C_BAR_BG.r, C_BAR_BG.g, C_BAR_BG.b, 255);
    SDL_RenderFillRectF(renderer, &(SDL_FRect){ 0, 0, (float)ow, oy + TOPBAR_H * scale });
    float by = oy + BOTTOMBAR_Y * scale;
    SDL_RenderFillRectF(renderer, &(SDL_FRect){ 0, by, (float)ow, oh - by });
    SDL_SetRenderDrawColor(renderer, C_DIVIDER.r, C_DIVIDER.g, C_DIVIDER.b, 255);
    SDL_RenderDrawLineF(renderer, 0, oy + TOPBAR_H * scale, (float)ow, oy + TOPBAR_H * scale);
    SDL_RenderDrawLineF(renderer, 0, by, (float)ow, by);

    SDL_RenderSetLogicalSize(renderer, W, H);
}

void display_draw(unsigned int now_ms)
{
    if (now_ms - last_text_ms >= TEXT_REFRESH_MS) {
        last_text_ms = now_ms;
        update_texts(now_ms);
    }

    approach(&anim_speed, target_speed);
    approach(&anim_volt, target_volt);
    approach(&anim_curr, target_curr);
    approach(&anim_batt, target_batt);
    approach(&anim_power, target_power);
    approach(&anim_balance, target_balance);

    fill_borders();
    if (background)
        SDL_RenderCopy(renderer, background, NULL, NULL);
    else
        draw_static();

    draw_topbar();
    draw_cards();
    draw_gauge();
    draw_tiles(now_ms);

    SDL_RenderPresent(renderer);
}

void display_close(void)
{
    for (int i = 0; i < T_COUNT; i++)
        gfx_text_free(&texts[i]);
    for (int i = 0; i < TILE_COUNT; i++)
        gfx_text_free(&tile_texts[i]);
    for (size_t i = 0; i < SDL_arraysize(font_defs); i++) {
        if (*font_defs[i].font)
            TTF_CloseFont(*font_defs[i].font);
    }
    if (background)
        SDL_DestroyTexture(background);
    if (logo)
        SDL_DestroyTexture(logo);
    if (renderer)
        SDL_DestroyRenderer(renderer);
    if (window)
        SDL_DestroyWindow(window);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
}
