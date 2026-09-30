#include "gfx.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#define DEG2RAD      ((float)M_PI / 180.0f)
#define CORNER_STEPS 6    /* Punkter pr. afrundet hjørne */
#define CIRCLE_STEPS 48
#define MAX_VERTS    256

static SDL_Renderer *ren;
static SDL_Vertex verts[MAX_VERTS];
static int indices[MAX_VERTS * 3];

void gfx_init(SDL_Renderer *renderer)
{
    ren = renderer;
}

static SDL_Vertex vert(float x, float y, SDL_Color c)
{
    SDL_Vertex v = { { x, y }, c, { 0, 0 } };
    return v;
}

/* Omkreds af et afrundet rektangel, med uret fra øverste venstre hjørne */
static int rounded_outline(SDL_FPoint *pts, float x, float y, float w, float h, float r)
{
    const float cx[4] = { x + w - r, x + w - r, x + r,     x + r };
    const float cy[4] = { y + r,     y + h - r, y + h - r, y + r };
    const float start[4] = { -90, 0, 90, 180 };
    int n = 0;

    for (int corner = 0; corner < 4; corner++) {
        for (int i = 0; i <= CORNER_STEPS; i++) {
            float a = (start[corner] + 90.0f * i / CORNER_STEPS) * DEG2RAD;
            pts[n++] = (SDL_FPoint){ cx[corner] + r * cosf(a), cy[corner] + r * sinf(a) };
        }
    }
    return n;
}

/* Fylder en konveks polygon som en vifte af trekanter fra midtpunktet */
static void fill_fan(float mx, float my, const SDL_FPoint *pts, int n, SDL_Color c)
{
    verts[0] = vert(mx, my, c);
    for (int i = 0; i < n; i++)
        verts[i + 1] = vert(pts[i].x, pts[i].y, c);

    int k = 0;
    for (int i = 0; i < n; i++) {
        indices[k++] = 0;
        indices[k++] = i + 1;
        indices[k++] = (i + 1) % n + 1;
    }
    SDL_RenderGeometry(ren, NULL, verts, n + 1, indices, k);
}

static void stroke_points(const SDL_FPoint *pts, int n, SDL_Color c)
{
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    SDL_RenderDrawLinesF(ren, pts, n);
    SDL_RenderDrawLineF(ren, pts[n - 1].x, pts[n - 1].y, pts[0].x, pts[0].y);
}

void gfx_fill_rounded(float x, float y, float w, float h, float r, SDL_Color c)
{
    SDL_FPoint pts[4 * (CORNER_STEPS + 1)];
    r = fminf(r, fminf(w, h) / 2);
    int n = rounded_outline(pts, x, y, w, h, r);
    fill_fan(x + w / 2, y + h / 2, pts, n, c);
}

void gfx_stroke_rounded(float x, float y, float w, float h, float r, SDL_Color c)
{
    SDL_FPoint pts[4 * (CORNER_STEPS + 1)];
    r = fminf(r, fminf(w, h) / 2);
    int n = rounded_outline(pts, x, y, w, h, r);
    stroke_points(pts, n, c);
}

static int circle_points(SDL_FPoint *pts, float cx, float cy, float r)
{
    for (int i = 0; i < CIRCLE_STEPS; i++) {
        float a = 360.0f * i / CIRCLE_STEPS * DEG2RAD;
        pts[i] = (SDL_FPoint){ cx + r * cosf(a), cy + r * sinf(a) };
    }
    return CIRCLE_STEPS;
}

void gfx_fill_circle(float cx, float cy, float r, SDL_Color c)
{
    SDL_FPoint pts[CIRCLE_STEPS];
    fill_fan(cx, cy, pts, circle_points(pts, cx, cy, r), c);
}

void gfx_stroke_circle(float cx, float cy, float r, SDL_Color c)
{
    SDL_FPoint pts[CIRCLE_STEPS];
    stroke_points(pts, circle_points(pts, cx, cy, r), c);
}

void gfx_line(float x0, float y0, float x1, float y1, float width, SDL_Color c)
{
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f)
        return;

    /* Vinkelret forskydning giver en firkant med den ønskede tykkelse */
    float nx = -dy / len * width / 2, ny = dx / len * width / 2;
    SDL_Vertex v[4] = {
        vert(x0 + nx, y0 + ny, c), vert(x1 + nx, y1 + ny, c),
        vert(x1 - nx, y1 - ny, c), vert(x0 - nx, y0 - ny, c),
    };
    const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(ren, NULL, v, 4, idx, 6);
}

static Uint8 lerp_u8(Uint8 a, Uint8 b, float t)
{
    return (Uint8)(a + (b - a) * t);
}

void gfx_arc(float cx, float cy, float r_in, float r_out, float a0, float a1,
             SDL_Color c0, SDL_Color c1)
{
    /* Ca. et segment pr. 3 grader - nok til at se rundt ud */
    int steps = (int)(fabsf(a1 - a0) / 3.0f) + 1;
    if (steps * 2 > MAX_VERTS)
        steps = MAX_VERTS / 2 - 1;

    for (int i = 0; i <= steps; i++) {
        float t = (float)i / steps;
        float a = (a0 + (a1 - a0) * t) * DEG2RAD;
        SDL_Color c = { lerp_u8(c0.r, c1.r, t), lerp_u8(c0.g, c1.g, t),
                        lerp_u8(c0.b, c1.b, t), lerp_u8(c0.a, c1.a, t) };
        /* Skærmens y-akse vender nedad, derfor minus */
        verts[i * 2]     = vert(cx + r_out * cosf(a), cy - r_out * sinf(a), c);
        verts[i * 2 + 1] = vert(cx + r_in * cosf(a),  cy - r_in * sinf(a),  c);
    }

    int k = 0;
    for (int i = 0; i < steps; i++) {
        int o = i * 2;
        indices[k++] = o;     indices[k++] = o + 1; indices[k++] = o + 2;
        indices[k++] = o + 1; indices[k++] = o + 3; indices[k++] = o + 2;
    }
    SDL_RenderGeometry(ren, NULL, verts, (steps + 1) * 2, indices, k);
}

static float align_x(float x, int w, gfx_align_t align)
{
    if (align == ALIGN_CENTER)
        return x - w / 2.0f;
    if (align == ALIGN_RIGHT)
        return x - w;
    return x;
}

static SDL_Texture *render_text(TTF_Font *font, SDL_Color c, const char *str, int *w, int *h)
{
    SDL_Surface *s = TTF_RenderUTF8_Blended(font, str, c);
    if (!s)
        return NULL;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(ren, s);
    *w = s->w;
    *h = s->h;
    SDL_FreeSurface(s);
    return tex;
}

int gfx_text(gfx_text_t *t, TTF_Font *font, SDL_Color c, const char *str,
             float x, float y, gfx_align_t align)
{
    bool changed = !t->tex || strcmp(t->str, str) != 0 ||
                   memcmp(&t->color, &c, sizeof c) != 0;
    if (changed) {
        gfx_text_free(t);
        t->tex = render_text(font, c, str, &t->w, &t->h);
        SDL_strlcpy(t->str, str, sizeof t->str);
        t->color = c;
    }
    if (!t->tex)
        return 0;

    SDL_FRect dst = { align_x(x, t->w, align), y, (float)t->w, (float)t->h };
    SDL_RenderCopyF(ren, t->tex, NULL, &dst);
    return t->w;
}

void gfx_text_free(gfx_text_t *t)
{
    if (t->tex)
        SDL_DestroyTexture(t->tex);
    t->tex = NULL;
}

int gfx_text_once(TTF_Font *font, SDL_Color c, const char *str, float x, float y,
                  gfx_align_t align)
{
    int w, h;
    SDL_Texture *tex = render_text(font, c, str, &w, &h);
    if (!tex)
        return 0;
    SDL_FRect dst = { align_x(x, w, align), y, (float)w, (float)h };
    SDL_RenderCopyF(ren, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
    return w;
}
