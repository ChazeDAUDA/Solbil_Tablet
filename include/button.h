#ifndef BUTTON_H
#define BUTTON_H

/*
 * Fysiske knapper på Raspberry Pi'ens GPIO.
 * Knapperne forbindes mellem GPIO-benet og GND; Pi'ens interne pull-up bruges.
 * Tryk og slip debounces og sendes videre til lights-modulet.
 */

/* Åbner GPIO-chippen (fx "/dev/gpiochip0"). Returnerer 0 ved succes. */
int button_init(const char *chip_path);

/* Læser knapperne. Kaldes ca. hvert 10. ms fra main-loopet. */
void button_poll(void);

void button_close(void);

#endif /* BUTTON_H */
