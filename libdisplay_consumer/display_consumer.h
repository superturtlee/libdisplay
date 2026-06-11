#ifndef DISPLAY_CONSUMER_H
#define DISPLAY_CONSUMER_H

#include <stdint.h>

#ifdef __ANDROID__
#include <android/native_window.h>
#else
typedef struct ANativeWindow_Buffer {
    int32_t  width;
    int32_t  height;
    int32_t  stride;
    int32_t  format;
    void    *bits;
    uint32_t reserved[6];
} ANativeWindow_Buffer;
#endif

#include "../common/protocol.h"

typedef struct display_ctx display_ctx;

int  connect_to_deamon(display_ctx **ctx, const char *socket_path);
void disconnect(display_ctx *ctx);
int  set_screen_info(display_ctx *ctx, uint32_t width, uint32_t height, uint32_t format);
int  push_buffer(display_ctx *ctx, ANativeWindow_Buffer *buf);
int  refresh_done(display_ctx *ctx);
int  push_input_event(display_ctx *ctx, const struct InputEvent *event);
int  set_fallback_callback(display_ctx *ctx, void (*on_fallback)(void *), void *userdata);

#endif
