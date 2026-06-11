#define _GNU_SOURCE
#include "display_producer.h"
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

    bool     async_pending;
    void    *mapped_buf;
    uint32_t mapped_size;
    int      mapped_fd;

    void (*fallback_cb)(void *);
    void  *fallback_userdata;
};

static void enter_fallback(display_ctx *ctx)
{
    if (ctx->fallback)
        return;
    ctx->fallback = true;

    if (ctx->mapped_buf) {
        munmap(ctx->mapped_buf, ctx->mapped_size);
        ctx->mapped_buf = NULL;
    }
    if (ctx->mapped_fd >= 0) { close(ctx->mapped_fd); ctx->mapped_fd = -1; }
    if (ctx->data_fd >= 0)         { close(ctx->data_fd);         ctx->data_fd = -1; }
    if (ctx->buf_ready_efd >= 0)   { close(ctx->buf_ready_efd);   ctx->buf_ready_efd = -1; }
    if (ctx->refresh_done_efd >= 0){ close(ctx->refresh_done_efd); ctx->refresh_done_efd = -1; }

    if (ctx->fallback_cb)
        ctx->fallback_cb(ctx->fallback_userdata);
}

static int pickup_fds(display_ctx *ctx)
{
    struct ctrl_msg hdr = { .type = CTRL_MSG_PICKUP_FDS, .size = 0 };
    if (send_all(ctx->ctrl_fd, &hdr, sizeof(hdr)) < 0)
        return -1;

    int fds[3];
    int fd_count = 0;
    struct ctrl_msg resp;
    int n = recv_fds(ctx->ctrl_fd, &resp, sizeof(resp), fds, 3, &fd_count);
    if (n <= 0 || resp.type != CTRL_MSG_FDS_READY || fd_count < 3)
        return -1;

    ctx->buf_ready_efd = fds[0];
    ctx->refresh_done_efd = fds[1];
    ctx->data_fd = fds[2];
    ctx->fallback = false;
    return 0;
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
    ctx->mapped_fd = -1;
    ctx->fallback = true;

    ctx->ctrl_fd = connect_unix(socket_path);
    if (ctx->ctrl_fd < 0)
        goto fail;

    struct ctrl_msg hdr = { .type = CTRL_MSG_PRODUCER_HELLO, .size = 0 };
    if (send_all(ctx->ctrl_fd, &hdr, sizeof(hdr)) < 0)
        goto fail;

    /* Block until screen info arrives */
    uint8_t buf[sizeof(struct ctrl_msg) + sizeof(struct screen_info)];
    if (recv_all(ctx->ctrl_fd, buf, sizeof(buf)) < 0)
        goto fail;

    struct ctrl_msg resp;
    memcpy(&resp, buf, sizeof(resp));
    if (resp.type != CTRL_MSG_SCREEN_INFO || resp.size != sizeof(struct screen_info))
        goto fail;

    struct screen_info si;
    memcpy(&si, buf + sizeof(struct ctrl_msg), sizeof(si));
    ctx->screen_w = si.width;
    ctx->screen_h = si.height;
    ctx->pixel_format = si.format;

    /* Pick up Consumer's fds (blocks until available) */
    if (pickup_fds(ctx) < 0)
        goto fail;

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
    if (ctx->mapped_buf)
        munmap(ctx->mapped_buf, ctx->mapped_size);
    if (ctx->mapped_fd >= 0)       close(ctx->mapped_fd);
    if (ctx->ctrl_fd >= 0)         close(ctx->ctrl_fd);
    if (ctx->data_fd >= 0)         close(ctx->data_fd);
    if (ctx->buf_ready_efd >= 0)   close(ctx->buf_ready_efd);
    if (ctx->refresh_done_efd >= 0) close(ctx->refresh_done_efd);
    free(ctx);
}

int get_screen_info(display_ctx *ctx, uint32_t *width, uint32_t *height, uint32_t *format)
{
    *width  = ctx->screen_w;
    *height = ctx->screen_h;
    *format = ctx->pixel_format;
    return 0;
}

int wait_buffer_async(display_ctx *ctx)
{
    ctx->async_pending = true;
    return 0;
}

static int ensure_mapped_buf(display_ctx *ctx)
{
    if (ctx->mapped_buf)
        return 0;

    uint8_t msg_buf[sizeof(struct data_msg) + sizeof(struct buf_info)];
    int fds[1];
    int fd_count = 0;

    int n = recv_fds(ctx->data_fd, msg_buf, sizeof(msg_buf), fds, 1, &fd_count);
    if (n < (int)sizeof(struct data_msg) || fd_count < 1) {
        enter_fallback(ctx);
        return -1;
    }

    struct data_msg hdr;
    memcpy(&hdr, msg_buf, sizeof(hdr));
    if (hdr.type != DATA_MSG_BUF_READY) {
        close(fds[0]);
        return -1;
    }

    struct buf_info bi;
    memcpy(&bi, msg_buf + sizeof(struct data_msg), sizeof(bi));

    void *mapped = mmap(NULL, bi.buf_size, PROT_READ | PROT_WRITE, MAP_SHARED, fds[0], 0);
    if (mapped == MAP_FAILED) {
        close(fds[0]);
        enter_fallback(ctx);
        return -1;
    }

    ctx->mapped_buf  = mapped;
    ctx->mapped_size = bi.buf_size;
    ctx->mapped_fd   = fds[0];
    return 0;
}

int wait_buffer_async_result(display_ctx *ctx, void **buffer)
{
    *buffer = NULL;

    if (ctx->fallback) {
        ctx->async_pending = false;
        if (pickup_fds(ctx) < 0)
            return -1;
    }

    if (!ctx->async_pending)
        return -1;

    struct pollfd pfd[2] = {
        { .fd = ctx->buf_ready_efd, .events = POLLIN },
        { .fd = ctx->data_fd,       .events = POLLIN | POLLHUP | POLLERR },
    };

    while (1) {
        int ret = poll(pfd, 2, 5000);
        if (ret < 0) {
            if (errno == EINTR) continue;
            enter_fallback(ctx);
            ctx->async_pending = false;
            return -1;
        }
        if (ret == 0) continue;

        if (pfd[1].revents & (POLLHUP | POLLERR)) {
            enter_fallback(ctx);
            ctx->async_pending = false;
            return -1;
        }
        if (pfd[0].revents & POLLIN)
            break;
    }

    eventfd_t val;
    eventfd_read(ctx->buf_ready_efd, &val);

    if (ensure_mapped_buf(ctx) < 0) {
        ctx->async_pending = false;
        return -1;
    }

    ctx->async_pending = false;
    *buffer = ctx->mapped_buf;
    return 0;
}

int trigger_refresh(display_ctx *ctx)
{
    if (ctx->fallback)
        return 0;

    eventfd_t val = 1;
    eventfd_write(ctx->refresh_done_efd, val);
    return 0;
}

int poll_input_event(display_ctx *ctx, struct InputEvent *event, int timeout_ms)
{
    if (ctx->fallback)
        return 0;

    struct pollfd pfd = { .fd = ctx->data_fd, .events = POLLIN };
    int ret = poll(&pfd, 1, timeout_ms);
    if (ret <= 0)
        return 0;

    if (pfd.revents & (POLLHUP | POLLERR)) {
        enter_fallback(ctx);
        return -1;
    }

    uint8_t msg_buf[sizeof(struct data_msg) + sizeof(struct InputEvent)];
    ssize_t n = recv(ctx->data_fd, msg_buf, sizeof(msg_buf), MSG_PEEK);
    if (n < (ssize_t)sizeof(struct data_msg))
        return 0;

    struct data_msg hdr;
    memcpy(&hdr, msg_buf, sizeof(hdr));
    if (hdr.type != DATA_MSG_INPUT_EVENT)
        return 0;

    if (recv_all(ctx->data_fd, msg_buf, sizeof(struct data_msg) + sizeof(struct InputEvent)) < 0)
        return -1;

    memcpy(event, msg_buf + sizeof(struct data_msg), sizeof(*event));
    return 1;
}

int set_fallback_callback(display_ctx *ctx, void (*on_fallback)(void *), void *userdata)
{
    ctx->fallback_cb = on_fallback;
    ctx->fallback_userdata = userdata;
    return 0;
}
