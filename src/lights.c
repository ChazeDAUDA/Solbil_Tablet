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
};

static bool state[LIGHT_COUNT];

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
        /* Slukkes kørelys, slukkes blinklys også */
        if (!state[LIGHT_RUNNING]) {
            if (state[LIGHT_TURN_LEFT])
                set_left(false);
            if (state[LIGHT_TURN_RIGHT])
                set_right(false);
        }
        break;

    case LIGHT_BRAKE:
        /* Bremsen er tændt så længe knappen holdes - slukkes i lights_release() */
        state[LIGHT_BRAKE] = true;
        send_cmd(LIGHT_ID_BRAKE, LIGHT_CMD_ON);
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
        /* Blinklys kræver kørelys og virker ikke mens havari er tændt */
        if (!state[LIGHT_RUNNING] || state[LIGHT_HAZARD])
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
    if (light == LIGHT_BRAKE && state[LIGHT_BRAKE]) {
        state[LIGHT_BRAKE] = false;
        send_cmd(LIGHT_ID_BRAKE, LIGHT_CMD_OFF);
        print_state();
    }
}

void lights_reset(void)
{
    memset(state, 0, sizeof state);
    send_cmd(LIGHT_ID_RUNNING, LIGHT_CMD_OFF);
    send_cmd(LIGHT_ID_BRAKE, LIGHT_CMD_OFF);
    send_cmd(LIGHT_ID_TURN_LEFT, LIGHT_CMD_OFF);
    send_cmd(LIGHT_ID_TURN_RIGHT, LIGHT_CMD_OFF);
    send_cmd(LIGHT_ID_HAZARD, LIGHT_CMD_OFF);
    print_state();
}
