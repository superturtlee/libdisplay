#define _GNU_SOURCE
#include "../libdisplay_consumer/display_consumer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WIDTH  64
#define HEIGHT 64
#define PIXEL_FORMAT_RGBA_8888 1
#define NUM_BUFS  3
#define NUM_FRAMES 30

static const uint32_t colors[3] = {
    0xFFFF0000, 0xFF00FF00, 0xFF0000FF,
};

static const char *color_name(uint32_t pixel)
{
    if (pixel == 0xFFFF0000) return "RED";
    if (pixel == 0xFF00FF00) return "GREEN";
    if (pixel == 0xFF0000FF) return "BLUE";
    return "UNKNOWN";
}

int main(int argc, char **argv)
{
    const char *sock = (argc > 1) ? argv[1] : "/tmp/display_daemon.sock";

    display_ctx *ctx;
    if (connect_to_deamon(&ctx, sock) < 0) {
        fprintf(stderr, "consumer: connect failed\n");
        return 1;
    }
    set_screen_info(ctx, WIDTH, HEIGHT, PIXEL_FORMAT_RGBA_8888);

    uint32_t *bufs[NUM_BUFS];
    for (int i = 0; i < NUM_BUFS; i++)
        bufs[i] = calloc(WIDTH * HEIGHT, sizeof(uint32_t));

    int errors = 0;
    int frame = 0;

    while (frame < NUM_FRAMES) {
        int idx = frame % NUM_BUFS;
        ANativeWindow_Buffer buf = {
            .width = WIDTH, .height = HEIGHT,
            .stride = WIDTH, .format = PIXEL_FORMAT_RGBA_8888,
            .bits = bufs[idx],
        };

        if (push_buffer(ctx, &buf) < 0) { usleep(10000); continue; }
        if (refresh_done(ctx) < 0) { usleep(10000); continue; }

        uint32_t got = bufs[idx][0];
        if (got == 0) { usleep(1000); continue; }

        uint32_t expect = colors[frame % 3];
        if (got != expect) {
            fprintf(stderr, "FAIL frame %d buf[%d]: expected %s got %s\n",
                    frame, idx, color_name(expect), color_name(got));
            errors++;
        }

        for (uint32_t i = 1; i < WIDTH * HEIGHT; i++) {
            if (bufs[idx][i] != got) {
                fprintf(stderr, "FAIL frame %d buf[%d] pixel[%u]: 0x%08X != 0x%08X\n",
                        frame, idx, i, bufs[idx][i], got);
                errors++;
                break;
            }
        }

        fprintf(stderr, "consumer: frame %2d buf[%d] = %-5s OK\n",
                frame, idx, color_name(got));
        frame++;
    }

    for (int i = 0; i < NUM_BUFS; i++)
        free(bufs[i]);
    disconnect(ctx);

    if (errors) {
        fprintf(stderr, "FAILED: %d errors\n", errors);
        return 1;
    }
    fprintf(stderr, "PASSED: %d frames, triple buffer verified\n", NUM_FRAMES);
    return 0;
}
