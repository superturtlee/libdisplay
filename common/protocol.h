#ifndef DISPLAY_PROTOCOL_H
#define DISPLAY_PROTOCOL_H

#include <stdint.h>

#define CTRL_MSG_CONSUMER_HELLO  1
#define CTRL_MSG_PRODUCER_HELLO  2
#define CTRL_MSG_SCREEN_INFO     7
#define CTRL_MSG_REJECT          8
#define CTRL_MSG_PICKUP_FDS      9
#define CTRL_MSG_FDS_READY      10

#define DATA_MSG_BUF_READY       100
#define DATA_MSG_REFRESH_DONE    101
#define DATA_MSG_INPUT_EVENT     102

struct ctrl_msg {
    uint32_t type;
    uint32_t size;
    uint8_t  payload[];
} __attribute__((packed));

struct data_msg {
    uint32_t type;
    uint32_t size;
    uint8_t  payload[];
} __attribute__((packed));

struct screen_info {
    uint32_t width;
    uint32_t height;
    uint32_t format;
} __attribute__((packed));

struct buf_info {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint32_t buf_size;
} __attribute__((packed));

#define INPUT_TYPE_TOUCH    1
#define INPUT_TYPE_KEY      2

struct InputEvent {
    uint32_t type;
    union {
        struct {
            int32_t  action;
            float    x;
            float    y;
            int32_t  pointer_id;
        } touch;
        struct {
            int32_t  action;
            int32_t  keycode;
            int32_t  meta_state;
        } key;
        struct {
            int32_t  action;
            float    x;
            float    y;
        } mouse;
    };
} __attribute__((packed));

#endif
