#ifndef CAN_BUS_H
#define CAN_BUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Tynd wrapper omkring Linux SocketCAN. Én can_bus_t pr. CAN-interface, så Pi'en
 * kan tale med flere busser på én gang:
 *   can_vesc   - klassisk CAN til VESC-motorcontrolleren (den kan ikke CAN FD)
 *   can_stm32  - lysprintet (klassiske 8-byte frames indtil videre, CAN FD senere)
 *
 * I dry-run mode åbnes der ingen socket; sendte frames printes kun til stdout.
 */

typedef struct {
    uint32_t id;
    bool extended;       /* 29-bit ID (VESC bruger extended ID'er) */
    uint8_t len;
    uint8_t data[8];
} can_msg_t;

typedef struct {
    const char *ifname;
    int sock;
    bool dry_run;
    bool failed_before;  /* Så genforsøg ikke fylder loggen med samme fejl */
} can_bus_t;

/* Sætter bussen op uden at åbne den. Kald can_bus_open() bagefter. */
void can_bus_init(can_bus_t *bus, const char *ifname, bool dry_run);

/*
 * Åbner og binder en CAN socket på bussens interface. Returnerer 0 ved succes.
 * Må kaldes igen efter en fejl - samme fejl logges kun første gang.
 */
int can_bus_open(can_bus_t *bus);

/* true hvis der er en åben socket (eller dry-run) */
bool can_bus_is_open(const can_bus_t *bus);

/* Sender en standard (11-bit) frame. len må være 0-8. Returnerer 0 ved succes. */
int can_bus_send(can_bus_t *bus, uint32_t id, const uint8_t *data, size_t len);

/* Læser én modtaget frame uden at blokere. Returnerer 1 hvis msg er udfyldt, ellers 0. */
int can_bus_recv(can_bus_t *bus, can_msg_t *msg);

void can_bus_close(can_bus_t *bus);

#endif /* CAN_BUS_H */
