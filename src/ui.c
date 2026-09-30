#include "ui.h"
#include "lights.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>

#define MARGIN           24
#define BLINK_PERIOD_MS  500
#define FRAME_TIMEOUT_MS 50
#define NO_FINGER        ((SDL_FingerID)-1)

/* Knap-layout: øverste række blinklys/havari, nederste række kørelys/bremse */
static const light_t row_top[]    = { LIGHT_TURN_LEFT, LIGHT_HAZARD, LIGHT_TURN_RIGHT };
static const light_t row_bottom[] = { LIGHT_RUNNING, LIGHT_BRAKE };

static const char *font_paths[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
    NULL
};

static const SDL_Color COLOR_BG        = {  18,  18,  22, 255 };
static const SDL_Color COLOR_OFF       = {  55,  58,  66, 255 };
static const SDL_Color COLOR_DISABLED  = {  32,  34,  38, 255 };
static const SDL_Color COLOR_LIGHT_ON  = {  40, 160, 255, 255 };
static const SDL_Color COLOR_BRAKE_ON  = { 220,  40,  40, 255 };
static const SDL_Color COLOR_BLINK_ON  = { 255, 170,   0, 255 };
static const SDL_Color COLOR_BLINK_DIM = { 120,  80,   0, 255 };
static const SDL_Color COLOR_TEXT      = { 240, 240, 240, 255 };

static SDL_Window *window;
static SDL_Renderer *renderer;
static TTF_Font *font;
static SDL_Rect buttons[LIGHT_COUNT];
static SDL_Texture *labels[LIGHT_COUNT];
static int screen_w, screen_h;

/* Den knap der holdes nede lige nu (til bremsen), og hvilken finger/mus der holder den */
static int held_light = -1;
static SDL_FingerID held_finger = NO_FINGER;

static void layout_row(const light_t *row, int count, int y, int h)
{
    int w = (screen_w - MARGIN * (count + 1)) / count;
    for (int i = 0; i < count; i++)
        buttons[row[i]] = (SDL_Rect){ MARGIN + i * (w + MARGIN), y, w, h };
}

static void layout_buttons(void)
{
    int h = (screen_h - MARGIN * 3) / 2;
    layout_row(row_top, SDL_arraysize(row_top), MARGIN, h);
    layout_row(row_bottom, SDL_arraysize(row_bottom), MARGIN * 2 + h, h);
}

static int load_labels(void)
{
    int size = screen_h / 14;
    for (int i = 0; font_paths[i] && !font; i++)
        font = TTF_OpenFont(font_paths[i], size);
    if (!font) {
        fprintf(stderr, "[ui] fandt ingen font (apt install fonts-dejavu-core)\n");
        return -1;
    }

    for (int i = 0; i < LIGHT_COUNT; i++) {
        SDL_Surface *s = TTF_RenderUTF8_Blended(font, lights_get(i)->label, COLOR_TEXT);
        if (!s)
            return -1;
        labels[i] = SDL_CreateTextureFromSurface(renderer, s);
        SDL_FreeSurface(s);
    }
    return 0;
}

int ui_init(bool fullscreen)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "[ui] SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    if (TTF_Init() != 0) {
        fprintf(stderr, "[ui] TTF_Init: %s\n", TTF_GetError());
        return -1;
    }

    Uint32 flags = fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
    window = SDL_CreateWindow("Solbil", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              1024, 600, flags);
    if (!window) {
        fprintf(stderr, "[ui] SDL_CreateWindow: %s\n", SDL_GetError());
        return -1;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        fprintf(stderr, "[ui] SDL_CreateRenderer: %s\n", SDL_GetError());
        return -1;
    }

    if (fullscreen)
        SDL_ShowCursor(SDL_DISABLE);

    SDL_GetRendererOutputSize(renderer, &screen_w, &screen_h);
    printf("[ui] skærm %dx%d\n", screen_w, screen_h);
    layout_buttons();
    return load_labels();
}

static SDL_Color button_color(light_t light, Uint32 now)
{
    if (!lights_is_enabled(light))
        return COLOR_DISABLED;
    if (!lights_is_on(light))
        return COLOR_OFF;
    if (light == LIGHT_BRAKE)
        return COLOR_BRAKE_ON;
    if (!lights_get(light)->blinking)
        return COLOR_LIGHT_ON;
    return ((now / BLINK_PERIOD_MS) % 2 == 0) ? COLOR_BLINK_ON : COLOR_BLINK_DIM;
}

static void draw(void)
{
    Uint32 now = SDL_GetTicks();

    SDL_SetRenderDrawColor(renderer, COLOR_BG.r, COLOR_BG.g, COLOR_BG.b, 255);
    SDL_RenderClear(renderer);

    for (int i = 0; i < LIGHT_COUNT; i++) {
        SDL_Color c = button_color(i, now);
        SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
        SDL_RenderFillRect(renderer, &buttons[i]);

        int tw, th;
        SDL_QueryTexture(labels[i], NULL, NULL, &tw, &th);
        SDL_Rect dst = { buttons[i].x + (buttons[i].w - tw) / 2,
                         buttons[i].y + (buttons[i].h - th) / 2, tw, th };
        SDL_SetTextureAlphaMod(labels[i], lights_is_enabled(i) ? 255 : 90);
        SDL_RenderCopy(renderer, labels[i], NULL, &dst);
    }

    SDL_RenderPresent(renderer);
}

static void handle_down(int x, int y, SDL_FingerID finger)
{
    SDL_Point p = { x, y };
    for (int i = 0; i < LIGHT_COUNT; i++) {
        if (SDL_PointInRect(&p, &buttons[i])) {
            lights_press(i);
            if (lights_get(i)->momentary) {
                held_light = i;
                held_finger = finger;
            }
            return;
        }
    }
}

static void handle_up(SDL_FingerID finger)
{
    if (held_light >= 0 && held_finger == finger) {
        lights_release(held_light);
        held_light = -1;
        held_finger = NO_FINGER;
    }
}

void ui_run(volatile sig_atomic_t *running)
{
    SDL_Event ev;

    while (*running) {
        if (SDL_WaitEventTimeout(&ev, FRAME_TIMEOUT_MS)) {
            do {
                switch (ev.type) {
                case SDL_QUIT:
                    *running = 0;
                    break;
                case SDL_KEYDOWN:
                    if (ev.key.keysym.sym == SDLK_ESCAPE)
                        *running = 0;
                    break;
                /* Touch-koordinater er normaliseret 0.0-1.0 */
                case SDL_FINGERDOWN:
                    handle_down((int)(ev.tfinger.x * screen_w), (int)(ev.tfinger.y * screen_h),
                                ev.tfinger.fingerId);
                    break;
                case SDL_FINGERUP:
                    handle_up(ev.tfinger.fingerId);
                    break;
                /* SDL laver også mus-events ud fra touch; dem ignorerer vi */
                case SDL_MOUSEBUTTONDOWN:
                    if (ev.button.which != SDL_TOUCH_MOUSEID)
                        handle_down(ev.button.x, ev.button.y, 0);
                    break;
                case SDL_MOUSEBUTTONUP:
                    if (ev.button.which != SDL_TOUCH_MOUSEID)
                        handle_up(0);
                    break;
                }
            } while (SDL_PollEvent(&ev));
        }
        draw();
    }

    /* Slip bremsen hvis programmet lukkes mens den holdes */
    if (held_light >= 0)
        lights_release(held_light);
}

void ui_close(void)
{
    for (int i = 0; i < LIGHT_COUNT; i++) {
        if (labels[i])
            SDL_DestroyTexture(labels[i]);
    }
    if (font)
        TTF_CloseFont(font);
    if (renderer)
        SDL_DestroyRenderer(renderer);
    if (window)
        SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
}
