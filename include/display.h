#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdbool.h>

/*
 * Skærmen i bilen. Lige nu: en indikatorlampe ("LED") pr. lys.
 * Senere udvides den til dashboard med hastighed, batteri osv.
 */

/* Opretter vindue, renderer og font. Returnerer 0 ved succes. */
int display_init(bool fullscreen);

/* Håndterer vindue-events. Returnerer false hvis programmet skal lukke (ESC / luk vindue). */
bool display_handle_events(void);

/* Tegner skærmen ud fra den nuværende tilstand */
void display_draw(unsigned int now_ms);

void display_close(void);

#endif /* DISPLAY_H */
