#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PC_VERSION 1U
#define PC_HEADER 16U
#define PC_PAYLOAD_MAX 2048U
#define PC_FRAME_MAX (PC_HEADER + PC_PAYLOAD_MAX)
#define PC_AUDIO_MAX 256U
#define PC_TX_MAX (PC_HEADER + PC_AUDIO_MAX)
#define PC_HEARTBEAT_MS 3000U
#define PC_TIMEOUT_MS 7000U

enum pc_message_type {
    PC_HELLO = 1,
    /* IDs 2-4 are retired. Never reuse session messages. */
    PC_MIC = 5,
    PC_MIC_ACK = 6,
    PC_AUDIO = 7,
    PC_HEARTBEAT = 8,
    PC_ERROR = 9,
    PC_CAPABILITIES = 10,
    PC_TIME = 11,
    PC_WEATHER = 12,
    PC_SETTINGS_UPDATE = 13,
    PC_DEVICE_STATUS = 14,
    PC_TIMER_STATUS = 15,
    PC_SETTINGS_ACK = 16,
    PC_STOP = 17,
    PC_STOP_ACK = 18
};
typedef struct {
    uint8_t type;
    uint32_t request, stream;
    uint16_t sequence, length;
    const uint8_t *payload;
} pc_message_t;
typedef struct {
    uint8_t data[PC_FRAME_MAX];
    size_t used;
} pc_parser_t;
typedef void (*pc_receive_fn)(const pc_message_t *, void *);

size_t pc_encode(uint8_t *out, size_t capacity, const pc_message_t *message);
void pc_feed(pc_parser_t *, const uint8_t *, size_t, pc_receive_fn, void *);
bool pc_utf8(const uint8_t *, size_t);
uint32_t pc_u32(const uint8_t *);
void pc_put32(uint8_t *, uint32_t);
