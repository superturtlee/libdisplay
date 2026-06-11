#ifndef DISPLAY_PRODUCER_H
#define DISPLAY_PRODUCER_H

#include <stdint.h>
#include "../common/protocol.h"

typedef struct display_ctx display_ctx;

int  connect_to_deamon(display_ctx **ctx, const char *socket_path);
void disconnect(display_ctx *ctx);
int  get_screen_info(display_ctx *ctx, uint32_t *width, uint32_t *height, uint32_t *format);
int  wait_buffer_async(display_ctx *ctx);
int  wait_buffer_async_result(display_ctx *ctx, void **buffer);
int  trigger_refresh(display_ctx *ctx);
int  poll_input_event(display_ctx *ctx, struct InputEvent *event, int timeout_ms);
int  set_fallback_callback(display_ctx *ctx, void (*on_fallback)(void *), void *userdata);

#endif
