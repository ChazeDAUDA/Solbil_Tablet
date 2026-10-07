#include "button.h"
#include "lights.h"
#include "time_ms.h"

#include <fcntl.h>
#include <linux/gpio.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define DEBOUNCE_MS 50

typedef struct {
    unsigned int gpio;     /* BCM GPIO-nummer (ikke fysisk pin-nummer) */
    light_t light;
    bool raw;              /* Seneste aflæsning */
    bool stable;           /* Debounced tilstand: true = trykket */
    unsigned int last_change;
} button_t;

/*
 * Benene holder Waveshare 2-CH CAN FD HAT fri: SPI0 (GPIO 8-11), SPI1 (GPIO 18-21)
 * og interrupts (GPIO 24-25). I2C (GPIO 2-3) er også fri.
 */
static button_t buttons[] = {
    {  5, LIGHT_RUNNING,    false, false, 0 },   /* Fysisk pin 29 */
    {  6, LIGHT_BRAKE,      false, false, 0 },   /* Fysisk pin 31 */
    { 13, LIGHT_TURN_LEFT,  false, false, 0 },   /* Fysisk pin 33 */
    { 22, LIGHT_TURN_RIGHT, false, false, 0 },   /* Fysisk pin 15 */
    { 26, LIGHT_HAZARD,     false, false, 0 },   /* Fysisk pin 37 */
};

#define BUTTON_COUNT (sizeof buttons / sizeof buttons[0])

static int line_fd = -1;

int button_init(const char *chip_path)
{
    int chip = open(chip_path, O_RDWR | O_CLOEXEC);
    if (chip < 0) {
        perror("[knap] open gpiochip");
        return -1;
    }

    struct gpio_v2_line_request req;
    memset(&req, 0, sizeof req);
    for (size_t i = 0; i < BUTTON_COUNT; i++)
        req.offsets[i] = buttons[i].gpio;
    req.num_lines = BUTTON_COUNT;
    strncpy(req.consumer, "solbil-tablet", sizeof req.consumer - 1);

    /* ACTIVE_LOW: knappen trækker benet til GND, så værdien 1 betyder "trykket" */
    req.config.flags = GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_BIAS_PULL_UP |
                       GPIO_V2_LINE_FLAG_ACTIVE_LOW;

    if (ioctl(chip, GPIO_V2_GET_LINE_IOCTL, &req) < 0) {
        perror("[knap] GPIO_V2_GET_LINE_IOCTL");
        close(chip);
        return -1;
    }
    close(chip);
    line_fd = req.fd;

    printf("[knap] %zu knapper klar på %s\n", BUTTON_COUNT, chip_path);
    return 0;
}

void button_poll(void)
{
    struct gpio_v2_line_values values;
    memset(&values, 0, sizeof values);
    values.mask = (1ULL << BUTTON_COUNT) - 1;

    if (ioctl(line_fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &values) < 0) {
        perror("[knap] GPIO_V2_LINE_GET_VALUES_IOCTL");
        return;
    }

    unsigned int now = time_ms();

    for (size_t i = 0; i < BUTTON_COUNT; i++) {
        button_t *b = &buttons[i];
        bool raw = (values.bits >> i) & 1;

        if (raw != b->raw) {
            b->raw = raw;
            b->last_change = now;
        } else if (raw != b->stable && now - b->last_change >= DEBOUNCE_MS) {
            b->stable = raw;
            if (raw)
                lights_press(b->light);
            else
                lights_release(b->light);
        }
    }
}

void button_close(void)
{
    if (line_fd >= 0)
        close(line_fd);
    line_fd = -1;
}
