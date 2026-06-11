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

static void phase_throughput(display_ctx *ctx, int duration_sec)
{
    fprintf(stderr, "\n--- Phase 1: Throughput (max speed, %ds) ---\n", duration_sec);

    uint64_t total = 0;
    uint64_t interval_count = 0;
    double start = now_sec();
    double deadline = start + duration_sec;
    double interval_start = start;

    double min_lat = 1e9, max_lat = 0, sum_lat = 0;

    while (running && now_sec() < deadline) {
        struct InputEvent ev;
        double t0 = now_sec();
        int ret = poll_input_event(ctx, &ev, 100);
        if (ret <= 0)
            continue;

        double lat = now_sec() - t0;
        if (lat < min_lat) min_lat = lat;
        if (lat > max_lat) max_lat = lat;
        sum_lat += lat;

        total++;
        interval_count++;

        double now = now_sec();
        if (now - interval_start >= 1.0) {
            fprintf(stderr, "  [%2ds] recv %lu ev/s  (poll lat: %.3f ms)\n",
                    (int)(now - start), (unsigned long)interval_count,
                    interval_count ? (sum_lat / interval_count) * 1000.0 : 0);
            interval_count = 0;
            interval_start = now;
            sum_lat = 0;
        }
    }

    double elapsed = now_sec() - start;
    fprintf(stderr, "\n  Throughput results:\n");
    fprintf(stderr, "    Total received: %lu events\n", (unsigned long)total);
    fprintf(stderr, "    Avg throughput: %.0f ev/s\n", total / elapsed);
    fprintf(stderr, "    Min poll lat:   %.3f ms\n", min_lat * 1000.0);
    fprintf(stderr, "    Max poll lat:   %.3f ms\n", max_lat * 1000.0);
}

static void phase_slow_reader(display_ctx *ctx, int duration_sec, int delay_us)
{
    fprintf(stderr, "\n--- Phase 2: Slow reader (%dμs delay, %ds) ---\n", delay_us, duration_sec);

    uint64_t total = 0;
    uint64_t interval_count = 0;
    uint64_t batch_max = 0;
    double start = now_sec();
    double deadline = start + duration_sec;
    double interval_start = start;

    while (running && now_sec() < deadline) {
        usleep(delay_us);

        uint64_t batch = 0;
        struct InputEvent ev;
        while (poll_input_event(ctx, &ev, 0) > 0)
            batch++;

        if (batch > batch_max)
            batch_max = batch;
        total += batch;
        interval_count += batch;

        double now = now_sec();
        if (now - interval_start >= 1.0) {
            fprintf(stderr, "  [%2ds] recv %lu ev/s  (batch max: %lu)\n",
                    (int)(now - start), (unsigned long)interval_count,
                    (unsigned long)batch_max);
            interval_count = 0;
            batch_max = 0;
            interval_start = now;
        }
    }

    double elapsed = now_sec() - start;
    fprintf(stderr, "\n  Slow reader results:\n");
    fprintf(stderr, "    Read delay:     %d μs\n", delay_us);
    fprintf(stderr, "    Total received: %lu events\n", (unsigned long)total);
    fprintf(stderr, "    Avg throughput: %.0f ev/s\n", total / elapsed);
}

static void phase_accumulation(display_ctx *ctx, int pause_sec)
{
    fprintf(stderr, "\n--- Phase 3: Accumulation (stop reading for %ds, then drain) ---\n", pause_sec);
    fprintf(stderr, "  Pausing reads... (consumer keeps sending)\n");

    double pause_start = now_sec();
    while (running && now_sec() - pause_start < pause_sec)
        usleep(100000);

    if (!running) return;

    fprintf(stderr, "  Pause done. Draining backlog...\n");

    uint64_t drained = 0;
    int consecutive_empty = 0;
    double drain_start = now_sec();

    while (running && consecutive_empty < 5) {
        struct InputEvent ev;
        int ret = poll_input_event(ctx, &ev, 100);
        if (ret <= 0) {
            consecutive_empty++;
            continue;
        }
        consecutive_empty = 0;
        drained++;
    }

    double drain_elapsed = now_sec() - drain_start;
    double ev_bytes = drained * (double)(sizeof(struct data_msg) + sizeof(struct InputEvent));

    fprintf(stderr, "\n  Accumulation results:\n");
    fprintf(stderr, "    Pause duration:  %d s\n", pause_sec);
    fprintf(stderr, "    Backlog drained: %lu events\n", (unsigned long)drained);
    fprintf(stderr, "    Backlog size:    %.2f KB\n", ev_bytes / 1024.0);
    fprintf(stderr, "    Drain time:      %.3f ms\n", drain_elapsed * 1000.0);
    if (drained > 0)
        fprintf(stderr, "    Drain rate:      %.0f ev/s\n", drained / drain_elapsed);
    fprintf(stderr, "    NOTE: backlog limited by socket buffer (~%lu bytes)\n",
            (unsigned long)(ev_bytes > 0 ? ev_bytes : 0));
}

int main(int argc, char **argv)
{
    const char *sock = (argc > 1) ? argv[1] : "/tmp/display_daemon.sock";
    int throughput_sec = (argc > 2) ? atoi(argv[2]) : 5;
    int pause_sec = (argc > 3) ? atoi(argv[3]) : 3;
    int slow_delay_us = (argc > 4) ? atoi(argv[4]) : 1000;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    display_ctx *ctx;
    if (connect_to_deamon(&ctx, sock) < 0) {
        fprintf(stderr, "bench_input_producer: connect failed\n");
        return 1;
    }

    uint32_t w, h, fmt;
    get_screen_info(ctx, &w, &h, &fmt);
    fprintf(stderr, "bench_input_producer: connected, screen %ux%u\n", w, h);

    wait_buffer_async(ctx);
    void *buffer;
    if (wait_buffer_async_result(ctx, &buffer) != 0) {
        fprintf(stderr, "bench_input_producer: initial buffer failed\n");
        disconnect(ctx);
        return 1;
    }
    trigger_refresh(ctx);
    fprintf(stderr, "bench_input_producer: channel established\n");

    phase_throughput(ctx, throughput_sec);

    if (running)
        phase_slow_reader(ctx, throughput_sec, slow_delay_us);

    if (running)
        phase_accumulation(ctx, pause_sec);

    fprintf(stderr, "\n===== Input Event Benchmark Complete =====\n");

    disconnect(ctx);
    return 0;
}
