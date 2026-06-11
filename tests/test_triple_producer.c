#define _GNU_SOURCE
#include "../libdisplay_producer/display_producer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NUM_FRAMES 30

static const uint32_t colors[3] = {
    0xFFFF0000, 0xFF00FF00, 0xFF0000FF,
};

int main(int argc, char **argv)
{
    const char *sock = (argc > 1) ? argv[1] : "/tmp/display_daemon.sock";

    display_ctx *ctx;
    if (connect_to_deamon(&ctx, sock) < 0) {
        fprintf(stderr, "producer: connect failed\n");
        return 1;
    }

    uint32_t w, h, fmt;
    get_screen_info(ctx, &w, &h, &fmt);
    fprintf(stderr, "producer: screen %ux%u\n", w, h);

    for (int frame = 0; frame < NUM_FRAMES; frame++) {
        wait_buffer_async(ctx);
        void *buffer;
        if (wait_buffer_async_result(ctx, &buffer) != 0) {
            usleep(10000);
            frame--;
            continue;
        }

        uint32_t color = colors[frame % 3];
        uint32_t *pixels = buffer;
        for (uint32_t i = 0; i < w * h; i++)
            pixels[i] = color;

        trigger_refresh(ctx);
    }

    disconnect(ctx);
    fprintf(stderr, "producer: done\n");
    return 0;
}
