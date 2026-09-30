#include "vesc.h"

#include <math.h>

/* Packet ID'er fra VESC-firmwarens comm_can.h */
#define CAN_PACKET_STATUS    9
#define CAN_PACKET_STATUS_2  14
#define CAN_PACKET_STATUS_3  15
#define CAN_PACKET_STATUS_4  16
#define CAN_PACKET_STATUS_5  27

static vesc_data_t vesc;

/* VESC sender big-endian */
static int16_t get_i16(const uint8_t *d)
{
    return (int16_t)((d[0] << 8) | d[1]);
}

static int32_t get_i32(const uint8_t *d)
{
    return (int32_t)(((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) |
                     ((uint32_t)d[2] << 8) | d[3]);
}

bool vesc_handle_msg(const can_msg_t *msg, unsigned int now_ms)
{
    if (!msg->extended || msg->len < 8)
        return false;

    int packet_id = (msg->id >> 8) & 0xFF;
    int controller_id = msg->id & 0xFF;
    const uint8_t *d = msg->data;

    if (VESC_CONTROLLER_ID >= 0 && controller_id != VESC_CONTROLLER_ID)
        return false;

    switch (packet_id) {
    case CAN_PACKET_STATUS:
        vesc.erpm          = (float)get_i32(&d[0]);
        vesc.motor_current = get_i16(&d[4]) / 10.0f;
        vesc.duty          = get_i16(&d[6]) / 1000.0f;
        break;
    case CAN_PACKET_STATUS_2:
        vesc.amp_hours         = get_i32(&d[0]) / 1e4f;
        vesc.amp_hours_charged = get_i32(&d[4]) / 1e4f;
        break;
    case CAN_PACKET_STATUS_3:
        vesc.watt_hours         = get_i32(&d[0]) / 1e4f;
        vesc.watt_hours_charged = get_i32(&d[4]) / 1e4f;
        break;
    case CAN_PACKET_STATUS_4:
        vesc.temp_fet   = get_i16(&d[0]) / 10.0f;
        vesc.temp_motor = get_i16(&d[2]) / 10.0f;
        vesc.current_in = get_i16(&d[4]) / 10.0f;
        break;
    case CAN_PACKET_STATUS_5:
        vesc.voltage_in = get_i16(&d[4]) / 10.0f;
        break;
    default:
        return false;
    }

    vesc.controller_id = controller_id;
    vesc.last_update_ms = now_ms;
    return true;
}

const vesc_data_t *vesc_get(void)
{
    return &vesc;
}

bool vesc_is_alive(unsigned int now_ms)
{
    return vesc.last_update_ms != 0 && now_ms - vesc.last_update_ms < VESC_TIMEOUT_MS;
}

float vesc_speed_kmh(void)
{
    float wheel_rpm = vesc.erpm / MOTOR_POLE_PAIRS / GEAR_RATIO;
    return fabsf(wheel_rpm) * (float)M_PI * WHEEL_DIAMETER_M * 60.0f / 1000.0f;
}

float vesc_power_w(void)
{
    return vesc.voltage_in * vesc.current_in;
}

float vesc_battery_percent(void)
{
    /* Coulomb-tælling fra VESC'ens Ah-tællere - nulstilles når VESC'en genstarter */
    float used = vesc.amp_hours - vesc.amp_hours_charged;
    float pct = (BATTERY_CAPACITY_AH - used) / BATTERY_CAPACITY_AH * 100.0f;
    return fminf(fmaxf(pct, 0.0f), 100.0f);
}
