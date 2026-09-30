#ifndef TIME_MS_H
#define TIME_MS_H

#include <time.h>

/* Millisekunder siden et vilkårligt fast tidspunkt (monotont ur) */
static inline unsigned int time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned int)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

#endif /* TIME_MS_H */
