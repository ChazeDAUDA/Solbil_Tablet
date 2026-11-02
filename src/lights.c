#include "lights.h"
#include "can_bus.h"
#include "time_ms.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *labels[LIGHT_COUNT] = {
    [LIGHT_RUNNING]    = "Kørelys",
    [LIGHT_BRAKE]      = "Bremse",
    [LIGHT_TURN_LEFT]  = "Blink V",
    [LIGHT_TURN_RIGHT] = "Blink H",
    [LIGHT_HAZARD]     = "Havari",
    [LIGHT_HIGH_BEAM]  = "Fjernlys",
    [LIGHT_FOG]        = "Tågelys",
};

/* state[LIGHT_BRAKE] er bremselysets faktiske tilstand = pedal ELLER regenerering */
static bool state[LIGHT_COUNT];
static bool brake_pedal, brake_regen;

/* Tidspunkt hvor blinket startede - STM32 tænder lampen med det samme ved en blink-kommando */
static unsigned int blink_start_ms;

static void send_cmd(uint8_t light_id, uint8_t command)
{
    uint8_t data[CANFD_LIGHT_MSG_LEN];

    memset(data, 0, sizeof data);
    data[0] = light_id;
    data[1] = command;
    can_bus_send(CANFD_LIGHT_CMD_ID, data, sizeof data);
}

static void print_state(void)
{
    printf("[lys]");
    for (int i = 0; i < LIGHT_COUNT; i++)
        printf("  %s: %s", labels[i], state[i] ? "TÆNDT" : "slukket");
    printf("\n");
}

static void set_left(bool on)
{
    state[LIGHT_TURN_LEFT] = on;
    if (on)
        blink_start_ms = time_ms();
    send_cmd(LIGHT_ID_TURN_LEFT, on ? LIGHT_CMD_BLINK_LEFT : LIGHT_CMD_OFF);
}

static void set_right(bool on)
{
    state[LIGHT_TURN_RIGHT] = on;
    if (on)
        blink_start_ms = time_ms();
    send_cmd(LIGHT_ID_TURN_RIGHT, on ? LIGHT_CMD_BLINK_RIGHT : LIGHT_CMD_OFF);
}

/* Tænd/sluk et lys uden blink. Sender kun, hvis tilstanden ændrer sig. */
static void set_simple(light_t light, uint8_t light_id, bool on)
{
    if (state[light] == on)
        return;
    state[light] = on;
    send_cmd(light_id, on ? LIGHT_CMD_ON : LIGHT_CMD_OFF);
}

/* Bremselyset er tændt når pedalen er trykket eller der regenereres. Returnerer true ved ændring. */
static bool update_brake(void)
{
    bool on = brake_pedal || brake_regen;
    if (on == state[LIGHT_BRAKE])
        return false;
    state[LIGHT_BRAKE] = on;
    send_cmd(LIGHT_ID_BRAKE, on ? LIGHT_CMD_ON : LIGHT_CMD_OFF);
    return true;
}

const char *lights_label(light_t light)
{
    return labels[light];
}

bool lights_is_on(light_t light)
{
    return state[light];
}

bool lights_blink_phase(unsigned int now_ms)
{
    return ((now_ms - blink_start_ms) / BLINK_INTERVAL_MS) % 2 == 0;
}

void lights_press(light_t light)
{
    switch (light) {
    case LIGHT_RUNNING:
        state[LIGHT_RUNNING] = !state[LIGHT_RUNNING];
        send_cmd(LIGHT_ID_RUNNING, state[LIGHT_RUNNING] ? LIGHT_CMD_ON : LIGHT_CMD_OFF);
        /* Fjernlys og tågelys kræver kørelys (som button.c på STM32) */
        if (!state[LIGHT_RUNNING]) {
            set_simple(LIGHT_HIGH_BEAM, LIGHT_ID_HIGH_BEAM, false);
            set_simple(LIGHT_FOG, LIGHT_ID_FOG_LIGHT, false);
        }
        break;

    case LIGHT_HIGH_BEAM:
    case LIGHT_FOG:
        if (!state[LIGHT_RUNNING])
            return;
        set_simple(light, light == LIGHT_HIGH_BEAM ? LIGHT_ID_HIGH_BEAM : LIGHT_ID_FOG_LIGHT,
                   !state[light]);
        break;

    case LIGHT_BRAKE:
        /* Bremsepedalens kontakt - tændt så længe den er trykket, slukkes i lights_release() */
        brake_pedal = true;
        if (!update_brake())
            return;
        break;

    case LIGHT_HAZARD:
        state[LIGHT_HAZARD] = !state[LIGHT_HAZARD];
        if (state[LIGHT_HAZARD]) {
            if (state[LIGHT_TURN_LEFT])
                set_left(false);
            if (state[LIGHT_TURN_RIGHT])
                set_right(false);
            blink_start_ms = time_ms();
            send_cmd(LIGHT_ID_HAZARD, LIGHT_CMD_BLINK_HAZARD);
        } else {
            send_cmd(LIGHT_ID_HAZARD, LIGHT_CMD_OFF);
        }
        break;

    case LIGHT_TURN_LEFT:
    case LIGHT_TURN_RIGHT:
        /*
         * Blinklys virker uafhængigt af kørelys (iESC 3.31.2: man skal kunne blinke ved
         * overhaling). Under havari ignoreres de, ellers ville STM32 afbryde havariblinket.
         */
        if (state[LIGHT_HAZARD])
            return;
        if (light == LIGHT_TURN_LEFT) {
            if (state[LIGHT_TURN_RIGHT])
                set_right(false);
            set_left(!state[LIGHT_TURN_LEFT]);
        } else {
            if (state[LIGHT_TURN_LEFT])
                set_left(false);
            set_right(!state[LIGHT_TURN_RIGHT]);
        }
        break;

    default:
        return;
    }

    print_state();
}

void lights_release(light_t light)
{
    if (light == LIGHT_BRAKE) {
        brake_pedal = false;
        if (update_brake())
            print_state();
    }
}

void lights_update_regen(float battery_current_a, bool valid)
{
    bool regen = brake_regen;

    if (!valid)
        regen = false;
    else if (battery_current_a < -REGEN_BRAKE_ON_A)
        regen = true;
    else if (battery_current_a > -REGEN_BRAKE_OFF_A)
        regen = false;

    if (regen == brake_regen)
        return;
    brake_regen = regen;
    if (update_brake())
        print_state();
}

void lights_resend(void)
{
    send_cmd(LIGHT_ID_RUNNING, state[LIGHT_RUNNING] ? LIGHT_CMD_ON : LIGHT_CMD_OFF);
    send_cmd(LIGHT_ID_BRAKE, state[LIGHT_BRAKE] ? LIGHT_CMD_ON : LIGHT_CMD_OFF);
    send_cmd(LIGHT_ID_HIGH_BEAM, state[LIGHT_HIGH_BEAM] ? LIGHT_CMD_ON : LIGHT_CMD_OFF);
    send_cmd(LIGHT_ID_FOG_LIGHT, state[LIGHT_FOG] ? LIGHT_CMD_ON : LIGHT_CMD_OFF);
    if (state[LIGHT_HAZARD]) {
        send_cmd(LIGHT_ID_HAZARD, LIGHT_CMD_BLINK_HAZARD);
    } else {
        send_cmd(LIGHT_ID_TURN_LEFT, state[LIGHT_TURN_LEFT] ? LIGHT_CMD_BLINK_LEFT : LIGHT_CMD_OFF);
        send_cmd(LIGHT_ID_TURN_RIGHT, state[LIGHT_TURN_RIGHT] ? LIGHT_CMD_BLINK_RIGHT : LIGHT_CMD_OFF);
    }
}

void lights_reset(void)
{
    memset(state, 0, sizeof state);
    brake_pedal = false;
    brake_regen = false;
    send_cmd(LIGHT_ID_RUNNING, LIGHT_CMD_OFF);
    send_cmd(LIGHT_ID_BRAKE, LIGHT_CMD_OFF);
    send_cmd(LIGHT_ID_TURN_LEFT, LIGHT_CMD_OFF);
    send_cmd(LIGHT_ID_TURN_RIGHT, LIGHT_CMD_OFF);
    send_cmd(LIGHT_ID_HAZARD, LIGHT_CMD_OFF);
    send_cmd(LIGHT_ID_HIGH_BEAM, LIGHT_CMD_OFF);
    send_cmd(LIGHT_ID_FOG_LIGHT, LIGHT_CMD_OFF);
    print_state();
}
