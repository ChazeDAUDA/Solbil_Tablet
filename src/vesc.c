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
    vesc_status_t status;

    if (VESC_CONTROLLER_ID >= 0 && controller_id != VESC_CONTROLLER_ID)
        return false;

    switch (packet_id) {
    case CAN_PACKET_STATUS:
        vesc.erpm          = (float)get_i32(&d[0]);
        vesc.motor_current = get_i16(&d[4]) / 10.0f;
        vesc.duty          = get_i16(&d[6]) / 1000.0f;
        status = VESC_STATUS_1;
        break;
    case CAN_PACKET_STATUS_2:
        vesc.amp_hours         = get_i32(&d[0]) / 1e4f;
        vesc.amp_hours_charged = get_i32(&d[4]) / 1e4f;
        status = VESC_STATUS_2;
        break;
    case CAN_PACKET_STATUS_3:
        vesc.watt_hours         = get_i32(&d[0]) / 1e4f;
        vesc.watt_hours_charged = get_i32(&d[4]) / 1e4f;
        status = VESC_STATUS_3;
        break;
    case CAN_PACKET_STATUS_4:
        vesc.temp_fet   = get_i16(&d[0]) / 10.0f;
        vesc.temp_motor = get_i16(&d[2]) / 10.0f;
        vesc.current_in = get_i16(&d[4]) / 10.0f;
        status = VESC_STATUS_4;
        break;
    case CAN_PACKET_STATUS_5:
        vesc.tachometer = get_i32(&d[0]);
        vesc.voltage_in = get_i16(&d[4]) / 10.0f;
        status = VESC_STATUS_5;
        break;
    default:
        return false;
    }

    vesc.controller_id = controller_id;
    vesc.rx_ms[status] = now_ms ? now_ms : 1;   /* 0 betyder "aldrig modtaget" */
    return true;
}

const vesc_data_t *vesc_get(void)
{
    return &vesc;
}

bool vesc_fresh(vesc_status_t status, unsigned int now_ms)
{
    unsigned int t = vesc.rx_ms[status];
    return t != 0 && now_ms - t < VESC_TIMEOUT_MS;
}

bool vesc_is_alive(unsigned int now_ms)
{
    for (int i = 0; i < VESC_STATUS_COUNT; i++) {
        if (vesc_fresh(i, now_ms))
            return true;
    }
    return false;
}

static float wheel_circumference_m(void)
{
    return (float)M_PI * WHEEL_DIAMETER_M;
}

float vesc_speed_kmh(void)
{
    float wheel_rpm = vesc.erpm / MOTOR_POLE_PAIRS / GEAR_RATIO;
    return fabsf(wheel_rpm) * wheel_circumference_m() * 60.0f / 1000.0f;
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

float vesc_trip_km(void)
{
    /* Tachometeret tæller 6 pr. elektrisk omdrejning og nulstilles når VESC'en genstarter */
    float motor_revs = vesc.tachometer / (6.0f * MOTOR_POLE_PAIRS);
    return fabsf(motor_revs / GEAR_RATIO) * wheel_circumference_m() / 1000.0f;
}
