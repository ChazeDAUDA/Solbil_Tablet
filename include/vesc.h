#ifndef VESC_H
#define VESC_H

#include <stdbool.h>
#include <stdint.h>

#include "can_bus.h"

/*
 * Læser status-beskeder fra VESC 100/250 motorcontrolleren.
 *
 * VESC'en sender kun status, hvis det er slået til i VESC Tool:
 *   App Settings -> General -> CAN Status Message Mode = CAN_STATUS_1_2_3_4_5
 *   og en passende rate (fx 50 Hz).
 * Hver besked har extended ID = (packet_id << 8) | controller_id.
 */

/* TODO: Ret til bilens data */
#define VESC_CONTROLLER_ID   -1      /* -1 = accepter alle controller-ID'er */
#define MOTOR_POLE_PAIRS     7       /* Motorens antal polpar (poler / 2) */
#define GEAR_RATIO           1.0f    /* Motoromdrejninger pr. hjulomdrejning */
#define WHEEL_DIAMETER_M     0.50f   /* Hjulets diameter i meter */
#define BATTERY_CAPACITY_AH  20.0f   /* Batteriets kapacitet i Ah */

#define VESC_TIMEOUT_MS      1000    /* Data ældre end dette regnes som tabt */

/* De fem status-beskeder - hver værdi kommer fra én af dem */
typedef enum {
    VESC_STATUS_1,   /* ERPM, motorstrøm, duty */
    VESC_STATUS_2,   /* Ah brugt / ladet */
    VESC_STATUS_3,   /* Wh brugt / ladet */
    VESC_STATUS_4,   /* Temperaturer, batteristrøm */
    VESC_STATUS_5,   /* Tachometer, batterispænding */
    VESC_STATUS_COUNT
} vesc_status_t;

typedef struct {
    float erpm;               /* Elektrisk RPM (STATUS_1) */
    float motor_current;      /* A, motorstrøm (STATUS_1) */
    float duty;               /* 0-1 (STATUS_1) */
    float amp_hours;          /* Ah brugt siden VESC'en startede (STATUS_2) */
    float amp_hours_charged;  /* Ah ladet tilbage, fx regenerering (STATUS_2) */
    float watt_hours;         /* Wh brugt (STATUS_3) */
    float watt_hours_charged; /* Wh ladet (STATUS_3) */
    float temp_fet;           /* °C (STATUS_4) */
    float temp_motor;         /* °C (STATUS_4) */
    float current_in;         /* A, batteristrøm (STATUS_4) */
    int32_t tachometer;       /* Motor-tæller, 6 pr. elektrisk omdrejning (STATUS_5) */
    float voltage_in;         /* V, batterispænding (STATUS_5) */
    int controller_id;        /* ID på den VESC der senest sendte */
    unsigned int rx_ms[VESC_STATUS_COUNT];  /* Hvornår hver status sidst kom (0 = aldrig) */
} vesc_data_t;

/* Fortolker en modtaget frame. Returnerer true hvis det var en VESC-status. */
bool vesc_handle_msg(const can_msg_t *msg, unsigned int now_ms);

const vesc_data_t *vesc_get(void);

/* true hvis status-beskeden er modtaget inden for VESC_TIMEOUT_MS */
bool vesc_fresh(vesc_status_t status, unsigned int now_ms);

/* true hvis mindst én status-besked er frisk */
bool vesc_is_alive(unsigned int now_ms);

/* Afledte værdier til dashboardet */
float vesc_speed_kmh(void);       /* STATUS_1 */
float vesc_power_w(void);         /* STATUS_4 + STATUS_5 */
float vesc_battery_percent(void); /* STATUS_2 */
float vesc_trip_km(void);         /* STATUS_5 */

#endif /* VESC_H */
