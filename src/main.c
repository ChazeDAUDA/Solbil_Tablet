#include "can_fd.h"
#include "lights.h"
#include "ui.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;

static void on_signal(int sig)
{
    (void)sig;
    running = 0;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Brug: %s [-i interface] [-n] [-w]\n"
            "  -i  CAN interface (standard: can0)\n"
            "  -n  dry-run: print frames i stedet for at sende\n"
            "  -w  vindue i stedet for fullscreen\n",
            prog);
}

int main(int argc, char **argv)
{
    const char *ifname = "can0";
    bool dry_run = false;
    bool fullscreen = true;
    int opt;

    while ((opt = getopt(argc, argv, "i:nwh")) != -1) {
        switch (opt) {
        case 'i': ifname = optarg; break;
        case 'n': dry_run = true; break;
        case 'w': fullscreen = false; break;
        default:  usage(argv[0]); return EXIT_FAILURE;
        }
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    if (can_fd_init(ifname, dry_run) != 0)
        return EXIT_FAILURE;

    if (ui_init(fullscreen) != 0) {
        ui_close();
        can_fd_close();
        return EXIT_FAILURE;
    }

    /* Sørg for at printet starter i samme tilstand som skærmen (alt slukket) */
    lights_reset();

    ui_run(&running);

    ui_close();
    can_fd_close();
    return EXIT_SUCCESS;
}
