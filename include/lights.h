#ifndef LIGHTS_H
#define LIGHTS_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Protokol - SKAL matche fdcan_driver.h i Solbil_rearlights.
 * Hver besked er en 16-byte CAN FD frame: byte 0 = lightID, byte 1 = command.
 */
#define CANFD_LIGHT_CMD_ID      0x123
#define CANFD_LIGHT_MSG_LEN     16

#define LIGHT_CMD_OFF           0x00
#define LIGHT_CMD_ON            0x01
#define LIGHT_CMD_BLINK_LEFT    0x02
#define LIGHT_CMD_BLINK_RIGHT   0x03
#define LIGHT_CMD_BLINK_HAZARD  0x04

#define LIGHT_ID_RUNNING        0x01
#define LIGHT_ID_TURN_LEFT      0x02
#define LIGHT_ID_TURN_RIGHT     0x03
#define LIGHT_ID_BRAKE          0x04
#define LIGHT_ID_HAZARD         0x05

/* Knapperne på skærmen */
typedef enum {
    LIGHT_TURN_LEFT,
    LIGHT_HAZARD,
    LIGHT_TURN_RIGHT,
    LIGHT_RUNNING,
    LIGHT_BRAKE,
    LIGHT_COUNT
} light_t;

typedef struct {
    const char *label;   /* Tekst på knappen */
    bool momentary;      /* Tændt så længe knappen holdes (bremse) */
    bool blinking;       /* Knappen blinker på skærmen når den er aktiv */
} light_def_t;

const light_def_t *lights_get(light_t light);
bool lights_is_on(light_t light);

/* false hvis knappen ikke har nogen effekt lige nu (fx blink uden kørelys) */
bool lights_is_enabled(light_t light);

/* Kaldes når en knap trykkes ned / slippes. Logikken følger button.c. */
void lights_press(light_t light);
void lights_release(light_t light);

/* Slukker alt på printet, så det matcher skærmen ved opstart. */
void lights_reset(void);

#endif /* LIGHTS_H */
