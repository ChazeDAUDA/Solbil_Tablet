#include "button.h"
#include "can_bus.h"
#include "display.h"
#include "lights.h"
#include "time_ms.h"
#include "vesc.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define POLL_INTERVAL_US  10000   /* Knapper og CAN læses hvert 10. ms */
#define PRINT_INTERVAL_MS 1000    /* VESC-data printes hvert sekund (indtil dashboardet er klar) */
#define DRAW_INTERVAL_MS  33      /* Skærmen tegnes ca. 30 gange i sekundet */

static volatile sig_atomic_t running = 1;

static void on_signal(int sig)
{
    (void)sig;
    running = 0;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Brug: %s [-i interface] [-c gpiochip] [-n]\n"
            "  -i  CAN interface (standard: can0)\n"
            "  -c  GPIO chip (standard: /dev/gpiochip0)\n"
            "  -n  dry-run: print frames i stedet for at sende\n",
            prog);
}

static void print_vesc(unsigned int now)
{
    static bool was_alive = true;

    /* Meld kun "ingen data" én gang, så knaptryk ikke drukner i terminalen */
    if (!vesc_is_alive(now)) {
        if (was_alive)
            printf("[vesc] ingen data\n");
        was_alive = false;
        return;
    }
    was_alive = true;

    const vesc_data_t *v = vesc_get();
    printf("[vesc] id %d  %5.1f km/h  %5.1f V  %6.1f A  %6.0f W  %3.0f %%  FET %.0f°C  motor %.0f°C\n",
           v->controller_id, vesc_speed_kmh(), v->voltage_in, v->current_in,
           vesc_power_w(), vesc_battery_percent(), v->temp_fet, v->temp_motor);
}

int main(int argc, char **argv)
{
    const char *ifname = "can0";
    const char *chip = "/dev/gpiochip0";
    bool dry_run = false;
    bool fullscreen = true;
    int opt;

    while ((opt = getopt(argc, argv, "i:c:nwh")) != -1) {
        switch (opt) {
        case 'i': ifname = optarg; break;
        case 'c': chip = optarg; break;
        case 'n': dry_run = true; break;
        case 'w': fullscreen = false; break;
        default:  usage(argv[0]); return EXIT_FAILURE;
        }
    }

    /* Stdout er ofte ikke en terminal under systemd - print linje for linje */
    setvbuf(stdout, NULL, _IOLBF, 0);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    if (can_bus_init(ifname, dry_run) != 0) {
        fprintf(stderr, "Tip: kør med -n for at teste uden CAN-hardware\n");
        return EXIT_FAILURE;
    }

    if (button_init(chip) != 0) {
        can_bus_close();
        return EXIT_FAILURE;
    }

    if (display_init(fullscreen) != 0) {
        display_close();
        button_close();
        can_bus_close();
        return EXIT_FAILURE;
    }

    /* Sørg for at printet starter i samme tilstand som Pi'en (alt slukket) */
    lights_reset();

    printf("Klar - tryk på knapperne (ESC eller Ctrl+C for at stoppe)\n");
    unsigned int last_print = time_ms();
    unsigned int last_draw = 0;

    while (running) {
        if (!display_handle_events())
            running = 0;

        button_poll();

        can_msg_t msg;
        while (can_bus_recv(&msg))
            vesc_handle_msg(&msg, time_ms());

        unsigned int now = time_ms();
        if (now - last_print >= PRINT_INTERVAL_MS) {
            last_print = now;
            print_vesc(now);
        }

        if (now - last_draw >= DRAW_INTERVAL_MS) {
            last_draw = now;
            display_draw(now);
        }

        usleep(POLL_INTERVAL_US);
    }

    display_close();
    button_close();
    can_bus_close();
    return EXIT_SUCCESS;
}
