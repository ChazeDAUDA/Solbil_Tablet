#ifndef GFX_H
#define GFX_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

/*
 * Små tegne-hjælpere oven på SDL2. Former tegnes som trekanter med
 * SDL_RenderGeometry, så GPU'en gør arbejdet i stedet for CPU'en.
 */

typedef enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT } gfx_align_t;

/* Tekst der kun renderes om når indholdet ændrer sig (tekst-rendering er dyrt) */
typedef struct {
    SDL_Texture *tex;
    char str[48];
    SDL_Color color;
    int w, h;
} gfx_text_t;

void gfx_init(SDL_Renderer *renderer);

void gfx_fill_rounded(float x, float y, float w, float h, float r, SDL_Color c);
void gfx_stroke_rounded(float x, float y, float w, float h, float r, SDL_Color c);
void gfx_fill_circle(float cx, float cy, float r, SDL_Color c);
void gfx_stroke_circle(float cx, float cy, float r, SDL_Color c);
void gfx_line(float x0, float y0, float x1, float y1, float width, SDL_Color c);

/*
 * Tykt cirkeludsnit fra vinkel a0 til a1 (grader, 0 = højre, 90 = op).
 * Farven glider fra c0 til c1 langs buen.
 */
void gfx_arc(float cx, float cy, float r_in, float r_out, float a0, float a1,
             SDL_Color c0, SDL_Color c1);

/* Tegner cachet tekst. Returnerer bredden i pixels. */
int gfx_text(gfx_text_t *t, TTF_Font *font, SDL_Color c, const char *str,
             float x, float y, gfx_align_t align);
void gfx_text_free(gfx_text_t *t);

/* Tegner tekst én gang uden cache - til den statiske baggrund */
int gfx_text_once(TTF_Font *font, SDL_Color c, const char *str, float x, float y,
                  gfx_align_t align);

#endif /* GFX_H */
