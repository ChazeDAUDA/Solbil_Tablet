#ifndef UI_H
#define UI_H

#include <signal.h>
#include <stdbool.h>

/* Opretter vindue, renderer og font. Returnerer 0 ved succes. */
int ui_init(bool fullscreen);

/* Kører event-loopet indtil vinduet lukkes, ESC trykkes eller *running bliver 0. */
void ui_run(volatile sig_atomic_t *running);

void ui_close(void);

#endif /* UI_H */
