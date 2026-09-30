#include "display.h"
#include "lights.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>

static const char *font_paths[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
    NULL
};

/* Relativt til mappen med den eksekverbare fil (build/) */
#define LOGO_PATH   "../assets/logo.png"
#define LOGO_MARGIN 16

static const SDL_Color COLOR_BG      = {  18,  18,  22, 255 };
static const SDL_Color COLOR_LED_OFF = {  50,  52,  58, 255 };
static const SDL_Color COLOR_GREEN   = {  40, 210,  90, 255 };
static const SDL_Color COLOR_RED     = { 230,  40,  40, 255 };
static const SDL_Color COLOR_AMBER   = { 255, 170,   0, 255 };
static const SDL_Color COLOR_TEXT    = { 220, 220, 220, 255 };

typedef struct {
    light_t light;
    SDL_Color on;
    bool blinking;
} led_t;

/* Rækkefølge fra venstre mod højre på skærmen */
static const led_t leds[] = {
    { LIGHT_TURN_LEFT,  COLOR_AMBER, true  },
    { LIGHT_RUNNING,    COLOR_GREEN, false },
    { LIGHT_BRAKE,      COLOR_RED,   false },
    { LIGHT_HAZARD,     COLOR_AMBER, true  },
    { LIGHT_TURN_RIGHT, COLOR_AMBER, true  },
};

#define LED_COUNT ((int)SDL_arraysize(leds))

static SDL_Window *window;
static SDL_Renderer *renderer;
static TTF_Font *font;
static SDL_Texture *labels[LED_COUNT];
static SDL_Texture *logo;
static SDL_Rect logo_rect;
static int screen_w, screen_h;

/* Logoets sorte tekst er usynlig på mørk baggrund - farv mørke pixels lyse */
static void lighten_dark_pixels(SDL_Surface *s)
{
    SDL_LockSurface(s);
    for (int y = 0; y < s->h; y++) {
        Uint32 *row = (Uint32 *)((Uint8 *)s->pixels + y * s->pitch);
        for (int x = 0; x < s->w; x++) {
            Uint8 r, g, b, a;
            SDL_GetRGBA(row[x], s->format, &r, &g, &b, &a);
            if (a > 0 && r < 90 && g < 90 && b < 90)
                row[x] = SDL_MapRGBA(s->format, COLOR_TEXT.r, COLOR_TEXT.g, COLOR_TEXT.b, a);
        }
    }
    SDL_UnlockSurface(s);
}

/* Logoet er pynt - mangler det, kører programmet videre uden */
static void load_logo(void)
{
    char path[512];
    char *base = SDL_GetBasePath();
    snprintf(path, sizeof path, "%s%s", base ? base : "./", LOGO_PATH);
    SDL_free(base);

    SDL_Surface *loaded = IMG_Load(path);
    if (!loaded) {
        fprintf(stderr, "[skærm] kunne ikke indlæse logo (%s): %s\n", path, IMG_GetError());
        return;
    }
    SDL_Surface *s = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(loaded);
    if (!s)
        return;

    lighten_dark_pixels(s);
    logo = SDL_CreateTextureFromSurface(renderer, s);

    /* Øverste venstre hjørne, 1/6 af skærmhøjden, samme højde/bredde-forhold */
    int h = screen_h / 6;
    logo_rect = (SDL_Rect){ LOGO_MARGIN, LOGO_MARGIN, s->w * h / s->h, h };
    SDL_FreeSurface(s);
}

static int load_labels(void)
{
    int size = screen_h / 18;
    for (int i = 0; font_paths[i] && !font; i++)
        font = TTF_OpenFont(font_paths[i], size);
    if (!font) {
        fprintf(stderr, "[skærm] fandt ingen font (apt install fonts-dejavu-core)\n");
        return -1;
    }

    for (int i = 0; i < LED_COUNT; i++) {
        SDL_Surface *s = TTF_RenderUTF8_Blended(font, lights_label(leds[i].light), COLOR_TEXT);
        if (!s)
            return -1;
        labels[i] = SDL_CreateTextureFromSurface(renderer, s);
        SDL_FreeSurface(s);
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

    Uint32 flags = fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
    window = SDL_CreateWindow("Solbil", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              1024, 600, flags);
    if (!window) {
        fprintf(stderr, "[skærm] SDL_CreateWindow: %s\n", SDL_GetError());
        return -1;
    }

    renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) {
        fprintf(stderr, "[skærm] SDL_CreateRenderer: %s\n", SDL_GetError());
        return -1;
    }

    if (fullscreen)
        SDL_ShowCursor(SDL_DISABLE);

    SDL_GetRendererOutputSize(renderer, &screen_w, &screen_h);
    printf("[skærm] %dx%d\n", screen_w, screen_h);
    load_logo();
    return load_labels();
}

bool display_handle_events(void)
{
    SDL_Event ev;

    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT)
            return false;
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
            return false;
    }
    return true;
}

static void fill_circle(int cx, int cy, int r)
{
    for (int dy = -r; dy <= r; dy++) {
        int dx = (int)SDL_sqrt((double)(r * r - dy * dy));
        SDL_RenderDrawLine(renderer, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

static bool led_is_lit(const led_t *led, unsigned int now_ms)
{
    if (!lights_is_on(led->light))
        return false;
    return !led->blinking || lights_blink_phase(now_ms);
}

void display_draw(unsigned int now_ms)
{
    SDL_SetRenderDrawColor(renderer, COLOR_BG.r, COLOR_BG.g, COLOR_BG.b, 255);
    SDL_RenderClear(renderer);

    if (logo)
        SDL_RenderCopy(renderer, logo, NULL, &logo_rect);

    int cell = screen_w / LED_COUNT;
    int r = SDL_min(cell, screen_h) / 4;
    int cy = screen_h / 2;

    for (int i = 0; i < LED_COUNT; i++) {
        int cx = cell * i + cell / 2;
        SDL_Color c = led_is_lit(&leds[i], now_ms) ? leds[i].on : COLOR_LED_OFF;

        SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
        fill_circle(cx, cy, r);

        int tw, th;
        SDL_QueryTexture(labels[i], NULL, NULL, &tw, &th);
        SDL_Rect dst = { cx - tw / 2, cy + r + th / 2, tw, th };
        SDL_RenderCopy(renderer, labels[i], NULL, &dst);
    }

    SDL_RenderPresent(renderer);
}

void display_close(void)
{
    for (int i = 0; i < LED_COUNT; i++) {
        if (labels[i])
            SDL_DestroyTexture(labels[i]);
    }
    if (logo)
        SDL_DestroyTexture(logo);
    if (font)
        TTF_CloseFont(font);
    if (renderer)
        SDL_DestroyRenderer(renderer);
    if (window)
        SDL_DestroyWindow(window);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
}
