#ifndef CAN_BUS_H
#define CAN_BUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Tynd wrapper omkring Linux SocketCAN.
 *
 * Bussen kører klassisk CAN (maks 8 bytes), fordi VESC-controlleren ikke
 * understøtter CAN FD - en FD-frame ville give error frames på hele bussen.
 *
 * I dry-run mode åbnes der ingen socket; sendte frames printes kun til stdout.
 */

typedef struct {
    uint32_t id;
    bool extended;       /* 29-bit ID (VESC bruger extended ID'er) */
    uint8_t len;
    uint8_t data[8];
} can_msg_t;

/* Åbner og binder en CAN socket på fx "can0". Returnerer 0 ved succes. */
int can_bus_init(const char *ifname, bool dry_run);

/* Sender en standard (11-bit) frame. len må være 0-8. Returnerer 0 ved succes. */
int can_bus_send(uint32_t id, const uint8_t *data, size_t len);

/* Læser én modtaget frame uden at blokere. Returnerer 1 hvis msg er udfyldt, ellers 0. */
int can_bus_recv(can_msg_t *msg);

void can_bus_close(void);

#endif /* CAN_BUS_H */
