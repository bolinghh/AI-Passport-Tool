#pragma once
#include "controller.h"
#include "esp_err.h"
typedef enum {
    PC_EVENT_BYTES,
    PC_EVENT_CONNECTED,
    PC_EVENT_DISCONNECTED,
    PC_EVENT_PAIR,
    PC_EVENT_KEY,
    PC_EVENT_AUDIO_ERROR
} pc_event_type_t;
typedef struct {
    pc_event_type_t type;
    uint32_t generation, value;
    uint16_t length;
    uint8_t key, event;
    uint8_t data[256];
} pc_event_t;
bool pc_post(const pc_event_t *);
esp_err_t pc_link_init(void);
bool pc_link_send(const pc_message_t *, bool audio);
uint32_t pc_link_generation(void);
bool pc_link_ready(void);
int pc_link_rssi(void);
void pc_link_sample_rssi(void);
unsigned pc_link_stack_margin(void);
void pc_link_drop(void);
esp_err_t pc_link_forget(void);
esp_err_t pc_capture_init(void);
void pc_capture_stream(uint32_t stream);
unsigned pc_capture_level(void);
unsigned pc_capture_stack_margin(void);
esp_err_t pc_screen_init(void);
void pc_screen_render(const pc_controller_t *, uint32_t passkey, bool pairing, int battery,
                      unsigned level, bool audio_ok, unsigned setting);

void pc_play_store_init(pc_play_t *);
void pc_play_store_tick(pc_play_t *,uint32_t now,bool safe);
