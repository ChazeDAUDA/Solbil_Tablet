#include "can_bus.h"

#include <errno.h>
#include <fcntl.h>
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

int can_bus_init(const char *ifname, bool dry_run)
{
    dry_run_mode = dry_run;
    if (dry_run) {
        printf("[can] dry-run: frames printes kun, intet sendes eller modtages\n");
        return 0;
    }

    sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) {
        perror("[can] socket");
        return -1;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        fprintf(stderr, "[can] interface '%s' findes ikke - se scripts/setup_can.sh\n", ifname);
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

    /* Ikke-blokerende, så main-loopet kan læse knapper imens */
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) | O_NONBLOCK);

    printf("[can] klar på %s\n", ifname);
    return 0;

fail:
    close(sock);
    sock = -1;
    return -1;
}

int can_bus_send(uint32_t id, const uint8_t *data, size_t len)
{
    if (len > CAN_MAX_DLEN)
        return -1;

    struct can_frame frame;
    memset(&frame, 0, sizeof frame);
    frame.can_id = id & CAN_SFF_MASK;
    frame.len = (uint8_t)len;
    memcpy(frame.data, data, len);

    printf("[can] TX 0x%03X [%u]", id, frame.len);
    for (int i = 0; i < frame.len; i++)
        printf(" %02X", frame.data[i]);
    printf("\n");

    if (dry_run_mode)
        return 0;

    if (write(sock, &frame, sizeof frame) != sizeof frame) {
        perror("[can] write");
        return -1;
    }
    return 0;
}

int can_bus_recv(can_msg_t *msg)
{
    if (dry_run_mode || sock < 0)
        return 0;

    struct can_frame frame;
    ssize_t n = read(sock, &frame, sizeof frame);
    if (n != sizeof frame) {
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
            perror("[can] read");
        return 0;
    }

    /* Error- og remote-frames er ikke data */
    if (frame.can_id & (CAN_ERR_FLAG | CAN_RTR_FLAG))
        return 0;

    msg->extended = (frame.can_id & CAN_EFF_FLAG) != 0;
    msg->id = frame.can_id & (msg->extended ? CAN_EFF_MASK : CAN_SFF_MASK);
    msg->len = frame.len;
    memcpy(msg->data, frame.data, sizeof msg->data);
    return 1;
}

void can_bus_close(void)
{
    if (sock >= 0)
        close(sock);
    sock = -1;
}
