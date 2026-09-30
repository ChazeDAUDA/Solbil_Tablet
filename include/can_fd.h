#ifndef CAN_FD_H
#define CAN_FD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Tynd wrapper omkring Linux SocketCAN til afsendelse af CAN FD frames.
 *
 * I dry-run mode åbnes der ingen socket; frames printes kun til stdout.
 * Det gør det muligt at teste UI'et på en maskine uden CAN-hardware.
 */

/* Åbner og binder en CAN FD socket på fx "can0". Returnerer 0 ved succes. */
int can_fd_init(const char *ifname, bool dry_run);

/*
 * Sender én CAN FD frame uden bit rate switch (BRS).
 * ID'er over 0x7FF sendes automatisk som extended (29-bit) ID.
 * len må være 0-64; frame-længden rundes op til nærmeste gyldige FD-længde.
 * Returnerer 0 ved succes.
 */
int can_fd_send(uint32_t id, const uint8_t *data, size_t len);

void can_fd_close(void);

#endif /* CAN_FD_H */
