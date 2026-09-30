#include "lights.h"
#include "can_fd.h"

#include <string.h>

static const light_def_t lights[LIGHT_COUNT] = {
    [LIGHT_TURN_LEFT]  = { "Blink V",  false, true  },
    [LIGHT_HAZARD]     = { "Havari",   false, true  },
    [LIGHT_TURN_RIGHT] = { "Blink H",  false, true  },
    [LIGHT_RUNNING]    = { "Kørelys",  false, false },
    [LIGHT_BRAKE]      = { "Bremse",   true,  false },
};

static bool state[LIGHT_COUNT];

static void send_cmd(uint8_t light_id, uint8_t command)
{
    uint8_t data[CANFD_LIGHT_MSG_LEN];

    memset(data, 0, sizeof data);
    data[0] = light_id;
    data[1] = command;
    can_fd_send(CANFD_LIGHT_CMD_ID, data, sizeof data);
}

static void set_left(bool on)
{
    state[LIGHT_TURN_LEFT] = on;
    send_cmd(LIGHT_ID_TURN_LEFT, on ? LIGHT_CMD_BLINK_LEFT : LIGHT_CMD_OFF);
}

static void set_right(bool on)
{
    state[LIGHT_TURN_RIGHT] = on;
    send_cmd(LIGHT_ID_TURN_RIGHT, on ? LIGHT_CMD_BLINK_RIGHT : LIGHT_CMD_OFF);
}

const light_def_t *lights_get(light_t light)
{
    return &lights[light];
}

bool lights_is_on(light_t light)
{
    return state[light];
}

bool lights_is_enabled(light_t light)
{
    if (light == LIGHT_TURN_LEFT || light == LIGHT_TURN_RIGHT)
        return state[LIGHT_RUNNING] && !state[LIGHT_HAZARD];
    return true;
}

void lights_press(light_t light)
{
    if (!lights_is_enabled(light))
        return;

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
        if (!state[LIGHT_BRAKE]) {
            state[LIGHT_BRAKE] = true;
            send_cmd(LIGHT_ID_BRAKE, LIGHT_CMD_ON);
        }
        break;

    case LIGHT_HAZARD:
        state[LIGHT_HAZARD] = !state[LIGHT_HAZARD];
        if (state[LIGHT_HAZARD]) {
            if (state[LIGHT_TURN_LEFT])
                set_left(false);
            if (state[LIGHT_TURN_RIGHT])
                set_right(false);
            send_cmd(LIGHT_ID_HAZARD, LIGHT_CMD_BLINK_HAZARD);
        } else {
            send_cmd(LIGHT_ID_HAZARD, LIGHT_CMD_OFF);
        }
        break;

    case LIGHT_TURN_LEFT:
        if (!state[LIGHT_TURN_LEFT]) {
            if (state[LIGHT_TURN_RIGHT])
                set_right(false);
            set_left(true);
        } else {
            set_left(false);
        }
        break;

    case LIGHT_TURN_RIGHT:
        if (!state[LIGHT_TURN_RIGHT]) {
            if (state[LIGHT_TURN_LEFT])
                set_left(false);
            set_right(true);
        } else {
            set_right(false);
        }
        break;

    default:
        break;
    }
}

void lights_release(light_t light)
{
    if (light == LIGHT_BRAKE && state[LIGHT_BRAKE]) {
        state[LIGHT_BRAKE] = false;
        send_cmd(LIGHT_ID_BRAKE, LIGHT_CMD_OFF);
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
}
