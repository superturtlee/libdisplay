#define _GNU_SOURCE
#include "display_consumer.h"
#include "../common/socket_utils.h"

#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

struct display_ctx {
    int      ctrl_fd;
    int      data_fd;
    int      buf_ready_efd;
    int      refresh_done_efd;
    uint32_t screen_w, screen_h;
    uint32_t pixel_format;
    bool     fallback;

    void    *shared_buf;
    uint32_t shared_buf_size;
    int      shared_memfd;
    void    *client_bits;
    bool     buffer_pending;

    void (*fallback_cb)(void *);
    void  *fallback_userdata;
};

static void enter_fallback(display_ctx *ctx)
{
    if (ctx->fallback)
        return;
    ctx->fallback = true;
    ctx->buffer_pending = false;

    if (ctx->shared_buf) {
        munmap(ctx->shared_buf, ctx->shared_buf_size);
        ctx->shared_buf = NULL;
    }
    if (ctx->shared_memfd >= 0) {
        close(ctx->shared_memfd);
        ctx->shared_memfd = -1;
    }

    if (ctx->data_fd >= 0)         { close(ctx->data_fd);         ctx->data_fd = -1; }
    if (ctx->buf_ready_efd >= 0)   { close(ctx->buf_ready_efd);   ctx->buf_ready_efd = -1; }
    if (ctx->refresh_done_efd >= 0){ close(ctx->refresh_done_efd); ctx->refresh_done_efd = -1; }

    ctx->buf_ready_efd = eventfd(0, EFD_CLOEXEC);
    ctx->refresh_done_efd = eventfd(0, EFD_CLOEXEC);
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) {
        ctx->data_fd = sv[0];
        struct ctrl_msg hdr = { .type = CTRL_MSG_CONSUMER_HELLO, .size = 0 };
        int fds[3] = { ctx->buf_ready_efd, ctx->refresh_done_efd, sv[1] };
        send_fds(ctx->ctrl_fd, &hdr, sizeof(hdr), fds, 3);
        close(sv[1]);
    }

    if (ctx->fallback_cb)
        ctx->fallback_cb(ctx->fallback_userdata);
}

int connect_to_deamon(display_ctx **out, const char *socket_path)
{
    display_ctx *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return -1;

    ctx->ctrl_fd = -1;
    ctx->data_fd = -1;
    ctx->buf_ready_efd = -1;
    ctx->refresh_done_efd = -1;
    ctx->shared_memfd = -1;
    ctx->fallback = true;

    ctx->ctrl_fd = connect_unix(socket_path);
    if (ctx->ctrl_fd < 0)
        goto fail;

    ctx->buf_ready_efd = eventfd(0, EFD_CLOEXEC);
    ctx->refresh_done_efd = eventfd(0, EFD_CLOEXEC);
    if (ctx->buf_ready_efd < 0 || ctx->refresh_done_efd < 0)
        goto fail;

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
        goto fail;
    ctx->data_fd = sv[0];

    struct ctrl_msg hdr = { .type = CTRL_MSG_CONSUMER_HELLO, .size = 0 };
    int fds[3] = { ctx->buf_ready_efd, ctx->refresh_done_efd, sv[1] };
    if (send_fds(ctx->ctrl_fd, &hdr, sizeof(hdr), fds, 3) < 0) {
        close(sv[1]);
        goto fail;
    }
    close(sv[1]);

    *out = ctx;
    return 0;

fail:
    if (ctx->ctrl_fd >= 0)         close(ctx->ctrl_fd);
    if (ctx->data_fd >= 0)         close(ctx->data_fd);
    if (ctx->buf_ready_efd >= 0)   close(ctx->buf_ready_efd);
    if (ctx->refresh_done_efd >= 0) close(ctx->refresh_done_efd);
    free(ctx);
    return -1;
}

void disconnect(display_ctx *ctx)
{
    if (!ctx)
        return;
    if (ctx->shared_buf)           munmap(ctx->shared_buf, ctx->shared_buf_size);
    if (ctx->shared_memfd >= 0)    close(ctx->shared_memfd);
    if (ctx->ctrl_fd >= 0)         close(ctx->ctrl_fd);
    if (ctx->data_fd >= 0)         close(ctx->data_fd);
    if (ctx->buf_ready_efd >= 0)   close(ctx->buf_ready_efd);
    if (ctx->refresh_done_efd >= 0) close(ctx->refresh_done_efd);
    free(ctx);
}

int set_screen_info(display_ctx *ctx, uint32_t width, uint32_t height, uint32_t format)
{
    ctx->screen_w = width;
    ctx->screen_h = height;
    ctx->pixel_format = format;

    struct ctrl_msg hdr = { .type = CTRL_MSG_SCREEN_INFO, .size = sizeof(struct screen_info) };
    struct screen_info si = { .width = width, .height = height, .format = format };
    uint8_t msg[sizeof(struct ctrl_msg) + sizeof(struct screen_info)];
    memcpy(msg, &hdr, sizeof(hdr));
    memcpy(msg + sizeof(hdr), &si, sizeof(si));
    return send_all(ctx->ctrl_fd, msg, sizeof(msg));
}

static int ensure_shared_buf(display_ctx *ctx, uint32_t size)
{
    if (ctx->shared_buf && ctx->shared_buf_size == size)
        return 0;

    if (ctx->shared_buf) {
        munmap(ctx->shared_buf, ctx->shared_buf_size);
        ctx->shared_buf = NULL;
    }
    if (ctx->shared_memfd >= 0) {
        close(ctx->shared_memfd);
        ctx->shared_memfd = -1;
    }

    int memfd = memfd_create("display_buf", MFD_CLOEXEC);
    if (memfd < 0)
        return -1;
    if (ftruncate(memfd, size) < 0) {
        close(memfd);
        return -1;
    }
    void *shared = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);
    if (shared == MAP_FAILED) {
        close(memfd);
        return -1;
    }

    ctx->shared_buf = shared;
    ctx->shared_buf_size = size;
    ctx->shared_memfd = memfd;

    struct buf_info bi = {
        .width   = ctx->screen_w,
        .height  = ctx->screen_h,
        .stride  = ctx->screen_w,
        .format  = ctx->pixel_format,
        .buf_size = size,
    };
    struct data_msg dhdr = { .type = DATA_MSG_BUF_READY, .size = sizeof(bi) };
    uint8_t msg[sizeof(struct data_msg) + sizeof(struct buf_info)];
    memcpy(msg, &dhdr, sizeof(dhdr));
    memcpy(msg + sizeof(dhdr), &bi, sizeof(bi));

    if (send_fds(ctx->data_fd, msg, sizeof(msg), &memfd, 1) < 0) {
        munmap(shared, size);
        close(memfd);
        ctx->shared_buf = NULL;
        ctx->shared_memfd = -1;
        enter_fallback(ctx);
        return -1;
    }
    return 0;
}

int push_buffer(display_ctx *ctx, ANativeWindow_Buffer *buf)
{
    if (ctx->fallback) {
        struct pollfd pfd = { .fd = ctx->ctrl_fd, .events = POLLIN };
        if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
            struct ctrl_msg hdr;
            if (recv_all(ctx->ctrl_fd, &hdr, sizeof(hdr)) == 0 &&
                hdr.type == CTRL_MSG_FDS_READY) {
                ctx->fallback = false;
            }
        }
        if (ctx->fallback)
            return 0;
    }

    uint32_t buf_size = buf->stride * buf->height * 4;
    if (ensure_shared_buf(ctx, buf_size) < 0)
        return -1;

    memcpy(ctx->shared_buf, buf->bits, buf_size);
    ctx->client_bits = buf->bits;

    eventfd_t val = 1;
    eventfd_write(ctx->buf_ready_efd, val);
    ctx->buffer_pending = true;
    return 0;
}

int refresh_done(display_ctx *ctx)
{
    if (!ctx->buffer_pending)
        return 0;

    struct pollfd pfd = { .fd = ctx->refresh_done_efd, .events = POLLIN };
    int ret = poll(&pfd, 1, 5000);
    if (ret <= 0) {
        enter_fallback(ctx);
        return -1;
    }

    eventfd_t val;
    eventfd_read(ctx->refresh_done_efd, &val);

    ctx->buffer_pending = false;

    if (ctx->shared_buf && ctx->client_bits)
        memcpy(ctx->client_bits, ctx->shared_buf, ctx->shared_buf_size);

    return 0;
}

int push_input_event(display_ctx *ctx, const struct InputEvent *event)
{
    if (ctx->fallback)
        return 0;

    struct data_msg hdr = { .type = DATA_MSG_INPUT_EVENT, .size = sizeof(struct InputEvent) };
    uint8_t msg[sizeof(struct data_msg) + sizeof(struct InputEvent)];
    memcpy(msg, &hdr, sizeof(hdr));
    memcpy(msg + sizeof(hdr), event, sizeof(*event));

    if (send_all(ctx->data_fd, msg, sizeof(msg)) < 0) {
        enter_fallback(ctx);
        return -1;
    }
    return 0;
}

int set_fallback_callback(display_ctx *ctx, void (*on_fallback)(void *), void *userdata)
{
    ctx->fallback_cb = on_fallback;
    ctx->fallback_userdata = userdata;
    return 0;
}
