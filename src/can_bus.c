#include "can_bus.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

static void report(can_bus_t *bus, const char *fmt, ...)
{
    if (bus->failed_before)
        return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[%s] ", bus->ifname);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void can_bus_init(can_bus_t *bus, const char *ifname, bool dry_run)
{
    bus->ifname = ifname;
    bus->sock = -1;
    bus->dry_run = dry_run;
    bus->failed_before = false;
}

int can_bus_open(can_bus_t *bus)
{
    if (bus->dry_run) {
        printf("[%s] dry-run: frames printes kun, intet sendes eller modtages\n", bus->ifname);
        return 0;
    }

    bus->sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (bus->sock < 0) {
        report(bus, "socket: %s\n", strerror(errno));
        bus->failed_before = true;
        return -1;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    strncpy(ifr.ifr_name, bus->ifname, IFNAMSIZ - 1);
    if (ioctl(bus->sock, SIOCGIFINDEX, &ifr) < 0) {
        report(bus, "interface findes ikke - prøver igen i baggrunden\n");
        goto fail;
    }

    struct sockaddr_can addr;
    memset(&addr, 0, sizeof addr);
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(bus->sock, (struct sockaddr *)&addr, sizeof addr) < 0) {
        report(bus, "bind: %s\n", strerror(errno));
        goto fail;
    }

    /* Ikke-blokerende, så main-loopet kan læse knapper imens */
    fcntl(bus->sock, F_SETFL, fcntl(bus->sock, F_GETFL) | O_NONBLOCK);

    printf("[%s] klar\n", bus->ifname);
    bus->failed_before = false;
    return 0;

fail:
    close(bus->sock);
    bus->sock = -1;
    bus->failed_before = true;
    return -1;
}

bool can_bus_is_open(const can_bus_t *bus)
{
    return bus->dry_run || bus->sock >= 0;
}

int can_bus_send(can_bus_t *bus, uint32_t id, const uint8_t *data, size_t len)
{
    if (len > CAN_MAX_DLEN)
        return -1;

    struct can_frame frame;
    memset(&frame, 0, sizeof frame);
    frame.can_id = id & CAN_SFF_MASK;
    frame.len = (uint8_t)len;
    memcpy(frame.data, data, len);

    printf("[%s] TX 0x%03X [%u]", bus->ifname, id, frame.len);
    for (int i = 0; i < frame.len; i++)
        printf(" %02X", frame.data[i]);
    printf("\n");

    if (bus->dry_run)
        return 0;
    if (bus->sock < 0)
        return -1;   /* Ingen CAN endnu - main prøver at åbne igen */

    if (write(bus->sock, &frame, sizeof frame) != sizeof frame) {
        /* Typisk ENOBUFS: ingen anden enhed på bussen kvitterer for beskederne */
        fprintf(stderr, "[%s] write: %s\n", bus->ifname, strerror(errno));
        return -1;
    }
    return 0;
}

int can_bus_recv(can_bus_t *bus, can_msg_t *msg)
{
    if (bus->dry_run || bus->sock < 0)
        return 0;

    struct can_frame frame;
    ssize_t n = read(bus->sock, &frame, sizeof frame);
    if (n != sizeof frame) {
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
            fprintf(stderr, "[%s] read: %s\n", bus->ifname, strerror(errno));
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

void can_bus_close(can_bus_t *bus)
{
    if (bus->sock >= 0)
        close(bus->sock);
    bus->sock = -1;
}
