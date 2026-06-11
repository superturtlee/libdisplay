#define _GNU_SOURCE
#include "../libdisplay_producer/display_producer.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile int running = 1;

static void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}

static const uint32_t colors[] = {
    0xFFFF0000,  /* RED   (ABGR) */
    0xFF00FF00,  /* GREEN */
    0xFF0000FF,  /* BLUE  */
};

int main(int argc, char **argv)
{
    const char *sock = (argc > 1) ? argv[1] : "/tmp/display_daemon.sock";

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    fprintf(stderr, "producer: connecting to %s ...\n", sock);

    display_ctx *ctx;
    if (connect_to_deamon(&ctx, sock) < 0) {
        fprintf(stderr, "producer: connect failed\n");
        return 1;
    }

    uint32_t w, h, fmt;
    get_screen_info(ctx, &w, &h, &fmt);
    fprintf(stderr, "producer: screen %ux%u fmt=%u\n", w, h, fmt);

    int frame = 0;
    while (running) {
        wait_buffer_async(ctx);

        void *buffer;
        if (wait_buffer_async_result(ctx, &buffer) != 0) {
            fprintf(stderr, "producer: wait_buffer failed (fallback)\n");
            usleep(500000);
            continue;
        }

        uint32_t color = colors[frame % 3];
        uint32_t *pixels = (uint32_t *)buffer;
        uint32_t count = w * h;
        for (uint32_t i = 0; i < count; i++)
            pixels[i] = color;

        trigger_refresh(ctx);

        const char *name = (frame % 3 == 0) ? "RED" :
                           (frame % 3 == 1) ? "GREEN" : "BLUE";
        fprintf(stderr, "producer: frame %d - %s\n", frame, name);
        frame++;
        usleep(500000);
    }

    disconnect(ctx);
    fprintf(stderr, "producer: exit\n");
    return 0;
}
