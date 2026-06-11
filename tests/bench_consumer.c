#define _GNU_SOURCE
#include "../libdisplay_consumer/display_consumer.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WIDTH  1920
#define HEIGHT 1080
#define PIXEL_FORMAT_RGBA_8888 1

static volatile int running = 1;

static void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}

int main(int argc, char **argv)
{
    const char *sock = (argc > 1) ? argv[1] : "/tmp/display_daemon.sock";

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    display_ctx *ctx;
    if (connect_to_deamon(&ctx, sock) < 0) {
        fprintf(stderr, "bench_consumer: connect failed\n");
        return 1;
    }

    set_screen_info(ctx, WIDTH, HEIGHT, PIXEL_FORMAT_RGBA_8888);
    fprintf(stderr, "bench_consumer: connected, %dx%d, waiting for producer...\n", WIDTH, HEIGHT);

    uint32_t buf_size = WIDTH * HEIGHT;
    uint32_t *frame_buffer = calloc(buf_size, sizeof(uint32_t));

    int frame = 0;
    while (running) {
        ANativeWindow_Buffer buf = {
            .width  = WIDTH,
            .height = HEIGHT,
            .stride = WIDTH,
            .format = PIXEL_FORMAT_RGBA_8888,
            .bits   = frame_buffer,
        };

        if (push_buffer(ctx, &buf) < 0) {
            usleep(10000);
            continue;
        }

        if (refresh_done(ctx) < 0) {
            usleep(10000);
            continue;
        }

        frame++;
    }

    free(frame_buffer);
    disconnect(ctx);
    fprintf(stderr, "bench_consumer: exit\n");
    return 0;
}
