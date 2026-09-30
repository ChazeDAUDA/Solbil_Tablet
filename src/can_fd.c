#include "can_fd.h"

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

static int sock = -1;
static bool dry_run_mode = false;

/* Gyldige datalængder for en CAN FD frame */
static const uint8_t fd_lengths[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64 };

static uint8_t fd_frame_len(size_t len)
{
    for (size_t i = 0; i < sizeof fd_lengths; i++) {
        if (fd_lengths[i] >= len)
            return fd_lengths[i];
    }
    return CANFD_MAX_DLEN;
}

int can_fd_init(const char *ifname, bool dry_run)
{
    dry_run_mode = dry_run;
    if (dry_run) {
        printf("[can] dry-run: frames printes kun, intet sendes\n");
        return 0;
    }

    sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) {
        perror("[can] socket");
        return -1;
    }

    int enable = 1;
    if (setsockopt(sock, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable, sizeof enable) < 0) {
        perror("[can] CAN_RAW_FD_FRAMES");
        goto fail;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

    /* Tjek at interfacet er sat op i FD mode (MTU = 72) */
    if (ioctl(sock, SIOCGIFMTU, &ifr) < 0) {
        fprintf(stderr, "[can] interface '%s' findes ikke\n", ifname);
        goto fail;
    }
    if (ifr.ifr_mtu != CANFD_MTU) {
        fprintf(stderr, "[can] '%s' er ikke i FD mode (mtu %d) - se scripts/setup_can.sh\n",
                ifname, ifr.ifr_mtu);
        goto fail;
    }

    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        perror("[can] SIOCGIFINDEX");
        goto fail;
    }

    struct sockaddr_can addr;
    memset(&addr, 0, sizeof addr);
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(sock, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("[can] bind");
        goto fail;
    }

    printf("[can] klar på %s\n", ifname);
    return 0;

fail:
    close(sock);
    sock = -1;
    return -1;
}

int can_fd_send(uint32_t id, const uint8_t *data, size_t len)
{
    if (len > CANFD_MAX_DLEN)
        return -1;

    struct canfd_frame frame;
    memset(&frame, 0, sizeof frame);
    frame.can_id = (id > CAN_SFF_MASK) ? ((id & CAN_EFF_MASK) | CAN_EFF_FLAG) : id;
    frame.len = fd_frame_len(len);
    frame.flags = 0;   /* Ingen BRS - printet kører FDCAN_FRAME_FD_NO_BRS */
    memcpy(frame.data, data, len);

    printf("[can] 0x%03X [%2u]", id, frame.len);
    for (int i = 0; i < frame.len; i++)
        printf(" %02X", frame.data[i]);
    printf("\n");

    if (dry_run_mode)
        return 0;

    if (write(sock, &frame, CANFD_MTU) != CANFD_MTU) {
        perror("[can] write");
        return -1;
    }
    return 0;
}

void can_fd_close(void)
{
    if (sock >= 0)
        close(sock);
    sock = -1;
}
