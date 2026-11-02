#ifndef LIGHTS_H
#define LIGHTS_H

#include <stdbool.h>

/*
 * Protokol - SKAL matche fdcan_driver.h i Solbil_rearlights.
 * Pi'en sender klassiske 8-byte frames (VESC kan ikke CAN FD); STM32 læser kun
 * byte 0 = lightID og byte 1 = command.
 */
#define CANFD_LIGHT_CMD_ID      0x123
#define CANFD_LIGHT_MSG_LEN     8

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

/*
 * Samme blinkinterval som BLINK_INTERVAL_MS i fdcan_driver.c.
 * iESC 2.24.12 kræver 90 ± 30 blink/min - 333 ms tændt + 333 ms slukket = 90 blink/min.
 */
#define BLINK_INTERVAL_MS       333

/*
 * Bremselys ved regenerativ bremsning (iESC 2.24.14). Hysterese, så lyset ikke
 * flimrer omkring grænsen: tænder under -ON, slukker igen over -OFF (batteristrøm i A).
 */
#define REGEN_BRAKE_ON_A        2.0f
#define REGEN_BRAKE_OFF_A       1.0f

typedef enum {
    LIGHT_RUNNING,
    LIGHT_BRAKE,
    LIGHT_TURN_LEFT,
    LIGHT_TURN_RIGHT,
    LIGHT_HAZARD,
    LIGHT_COUNT
} light_t;

const char *lights_label(light_t light);
bool lights_is_on(light_t light);

/* true hvis blinklys/havari er i den tændte halvdel af blinket lige nu */
bool lights_blink_phase(unsigned int now_ms);

/* Kaldes når en knap trykkes ned / slippes. */
void lights_press(light_t light);
void lights_release(light_t light);

/*
 * Kaldes løbende med VESC'ens batteristrøm. Negativ strøm = VESC'en lader batteriet
 * op = regenerativ bremsning, som også skal tænde bremselyset (uanset kørselsretning).
 * valid = false når der ikke er friske VESC-data.
 */
void lights_update_regen(float battery_current_a, bool valid);

/* Slukker alt på printet, så det matcher Pi'ens tilstand ved opstart. */
void lights_reset(void);

/* Sender den nuværende tilstand for alle lys igen, fx når CAN-forbindelsen kommer tilbage. */
void lights_resend(void);

#endif /* LIGHTS_H */
