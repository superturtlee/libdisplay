#define _GNU_SOURCE
#include "../libdisplay_consumer/display_consumer.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
    const char *sock = (argc > 1) ? argv[1] : "/tmp/display_daemon.sock";
    int duration_sec = (argc > 2) ? atoi(argv[2]) : 10;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    display_ctx *ctx;
    if (connect_to_deamon(&ctx, sock) < 0) {
        fprintf(stderr, "bench_input_consumer: connect failed\n");
        return 1;
    }

    set_screen_info(ctx, WIDTH, HEIGHT, PIXEL_FORMAT_RGBA_8888);

    uint32_t buf_size = WIDTH * HEIGHT;
    uint32_t *frame_buffer = calloc(buf_size, sizeof(uint32_t));

    ANativeWindow_Buffer buf = {
        .width  = WIDTH,
        .height = HEIGHT,
        .stride = WIDTH,
        .format = PIXEL_FORMAT_RGBA_8888,
        .bits   = frame_buffer,
    };

    fprintf(stderr, "bench_input_consumer: waiting for producer...\n");
    int established = 0;
    while (running && !established) {
        push_buffer(ctx, &buf);
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        int ret = refresh_done(ctx);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        long ns = (t1.tv_sec - t0.tv_sec) * 1000000000L + (t1.tv_nsec - t0.tv_nsec);
        if (ret == 0 && ns > 1000000)
            established = 1;
        else
            usleep(100000);
    }

    fprintf(stderr, "bench_input_consumer: channel ready, sending events for %ds\n", duration_sec);

    uint64_t seq = 0;
    uint64_t send_ok = 0;
    uint64_t send_fail = 0;
    double start = now_sec();
    double deadline = start + duration_sec;
    double interval_start = start;
    uint64_t interval_count = 0;

    while (running && now_sec() < deadline) {
        struct InputEvent ev;
        ev.type = INPUT_TYPE_TOUCH;
        ev.touch.action = 0;
        ev.touch.x = (float)(seq % WIDTH);
        ev.touch.y = (float)(seq % HEIGHT);
        ev.touch.pointer_id = 0;

        if (push_input_event(ctx, &ev) == 0) {
            send_ok++;
            interval_count++;
        } else {
            send_fail++;
        }
        seq++;

        double now = now_sec();
        if (now - interval_start >= 1.0) {
            fprintf(stderr, "  [%2ds] sent %lu ev/s\n",
                    (int)(now - start), (unsigned long)interval_count);
            interval_count = 0;
            interval_start = now;
        }
    }

    double elapsed = now_sec() - start;

    fprintf(stderr, "\n===== Input Event Send Results =====\n");
    fprintf(stderr, "  Duration:     %.1f s\n", elapsed);
    fprintf(stderr, "  Sent OK:      %lu\n", (unsigned long)send_ok);
    fprintf(stderr, "  Send fail:    %lu\n", (unsigned long)send_fail);
    fprintf(stderr, "  Throughput:   %.0f ev/s\n", send_ok / elapsed);
    fprintf(stderr, "  Avg size:     %zu bytes/ev\n",
            sizeof(struct data_msg) + sizeof(struct InputEvent));
    fprintf(stderr, "  Bandwidth:    %.2f MB/s\n",
            send_ok * (sizeof(struct data_msg) + sizeof(struct InputEvent)) / elapsed / (1024.0 * 1024.0));
    fprintf(stderr, "====================================\n");

    free(frame_buffer);
    disconnect(ctx);
    return 0;
}
