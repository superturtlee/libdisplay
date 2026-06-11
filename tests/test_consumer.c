#define _GNU_SOURCE
#include "../libdisplay_consumer/display_consumer.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WIDTH  64
#define HEIGHT 64
#define PIXEL_FORMAT_RGBA_8888 1

static volatile int running = 1;

static void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}

static const char *color_name(uint32_t pixel)
{
    uint32_t masked = pixel | 0xFF000000;
    if (masked == 0xFFFF0000) return "RED";
    if (masked == 0xFF00FF00) return "GREEN";
    if (masked == 0xFF0000FF) return "BLUE";
    return "UNKNOWN";
}

int main(int argc, char **argv)
{
    const char *sock = (argc > 1) ? argv[1] : "/tmp/display_daemon.sock";

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    fprintf(stderr, "consumer: connecting to %s ...\n", sock);

    display_ctx *ctx;
    if (connect_to_deamon(&ctx, sock) < 0) {
        fprintf(stderr, "consumer: connect failed\n");
        return 1;
    }

    set_screen_info(ctx, WIDTH, HEIGHT, PIXEL_FORMAT_RGBA_8888);
    fprintf(stderr, "consumer: connected, waiting for producer...\n");

    uint32_t *frame_buffer = calloc(WIDTH * HEIGHT, sizeof(uint32_t));

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
            fprintf(stderr, "consumer: push_buffer error\n");
            usleep(500000);
            continue;
        }

        if (refresh_done(ctx) < 0) {
            fprintf(stderr, "consumer: refresh_done error\n");
            usleep(500000);
            continue;
        }

        uint32_t pixel = frame_buffer[0];
        if (pixel != 0) {
            fprintf(stderr, "consumer: frame %d - %s (0x%08X)\n",
                    frame, color_name(pixel), pixel);
            frame++;
        } else {
            usleep(100000);
        }
    }

    free(frame_buffer);
    disconnect(ctx);
    fprintf(stderr, "consumer: exit\n");
    return 0;
}
