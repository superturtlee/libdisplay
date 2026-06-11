#define _GNU_SOURCE
#include "../libdisplay_producer/display_producer.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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
        fprintf(stderr, "bench_producer: connect failed\n");
        return 1;
    }

    uint32_t w, h, fmt;
    get_screen_info(ctx, &w, &h, &fmt);
    uint32_t buf_bytes = w * h * 4;
    fprintf(stderr, "bench_producer: screen %ux%u fmt=%u (%u bytes/frame)\n", w, h, fmt, buf_bytes);
    fprintf(stderr, "bench_producer: running for %d seconds...\n", duration_sec);

    int total_frames = 0;
    int interval_frames = 0;
    double start = now_sec();
    double interval_start = start;
    double deadline = start + duration_sec;

    double min_ft = 1e9, max_ft = 0, sum_ft = 0;

    while (running && now_sec() < deadline) {
        double t0 = now_sec();

        wait_buffer_async(ctx);

        if (total_frames == 0)
            fprintf(stderr, "bench_producer: waiting for first buffer...\n");

        void *buffer;
        if (wait_buffer_async_result(ctx, &buffer) != 0) {
            fprintf(stderr, "bench_producer: wait_buffer failed\n");
            usleep(1000);
            continue;
        }

        if (total_frames == 0)
            fprintf(stderr, "bench_producer: got buffer, writing...\n");

        memset(buffer, 0x42, buf_bytes);

        trigger_refresh(ctx);

        if (total_frames == 0)
            fprintf(stderr, "bench_producer: first frame done\n");

        double ft = now_sec() - t0;
        if (ft < min_ft) min_ft = ft;
        if (ft > max_ft) max_ft = ft;
        sum_ft += ft;

        total_frames++;
        interval_frames++;

        double now = now_sec();
        if (now - interval_start >= 1.0) {
            double fps = interval_frames / (now - interval_start);
            fprintf(stderr, "  [%2ds] %6.1f fps  (frame time: %.2f ms)\n",
                    (int)(now - start), fps, 1000.0 / fps);
            interval_frames = 0;
            interval_start = now;
        }
    }

    double elapsed = now_sec() - start;

    fprintf(stderr, "\n===== 1080P Benchmark Results =====\n");
    fprintf(stderr, "  Resolution:   %ux%u RGBA\n", w, h);
    fprintf(stderr, "  Frame size:   %.2f MB\n", buf_bytes / (1024.0 * 1024.0));
    fprintf(stderr, "  Duration:     %.1f s\n", elapsed);
    fprintf(stderr, "  Total frames: %d\n", total_frames);
    fprintf(stderr, "  Avg FPS:      %.1f\n", total_frames / elapsed);
    fprintf(stderr, "  Avg frame:    %.2f ms\n", (sum_ft / total_frames) * 1000.0);
    fprintf(stderr, "  Min frame:    %.2f ms\n", min_ft * 1000.0);
    fprintf(stderr, "  Max frame:    %.2f ms\n", max_ft * 1000.0);
    fprintf(stderr, "  Throughput:   %.1f MB/s\n",
            (total_frames * (double)buf_bytes) / elapsed / (1024.0 * 1024.0));
    fprintf(stderr, "===================================\n");

    disconnect(ctx);
    return 0;
}
