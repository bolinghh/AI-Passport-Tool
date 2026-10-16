#pragma once
#include "wire.h"
#include "play.h"
bool pc_subscription_lost(bool previous, bool current);
bool pc_capabilities(const pc_message_t *);
typedef enum { PC_OFF, PC_PREPARING, PC_LIVE } pc_mic_state_t;
typedef enum { PC_HOME, PC_APPS, PC_POMODORO, PC_MICROPHONE, PC_SETTINGS, PC_PET, PC_FISHING } pc_page_t;
typedef enum { PC_APP_POMODORO, PC_APP_MICROPHONE, PC_APP_PET, PC_APP_FISHING,
               PC_APP_SETTINGS, PC_APP_COUNT } pc_app_t;
pc_page_t pc_app_page(unsigned app);
unsigned pc_app_window(unsigned app);
typedef enum { PC_TIMER_IDLE, PC_TIMER_RUNNING, PC_TIMER_PAUSED, PC_TIMER_DONE } pc_timer_state_t;
typedef enum { PC_HINT_NONE, PC_HINT_OFFLINE, PC_HINT_ROUTE_FAILED, PC_HINT_TIMEOUT,
               PC_HINT_AUDIO_FAILED, PC_HINT_UPGRADE } pc_hint_t;
typedef struct {
    pc_timer_state_t state;
    bool rest, alert;
    uint8_t focus_minutes, rest_minutes;
    uint32_t remaining_ms, tick_at, completion;
} pc_timer_t;
typedef struct {
    bool ready, suppress_click, confirm, clock_valid, weather_valid;
    pc_page_t page;
    uint8_t app, operation, setting, brightness;
    pc_mic_state_t mic;
    pc_hint_t hint;
    uint32_t next_request, pending, stream, last_rx, request_at;
    uint32_t epoch, clock_at, weather_at;
    int32_t utc_offset;
    int16_t temperature;
    uint16_t weather_code;
    char city[49];
    pc_timer_t timer;
    pc_play_t play;
} pc_controller_t;
typedef struct { uint8_t type, value; uint32_t request, stream; } pc_action_t;
void pc_controller_init(pc_controller_t *);
void pc_controller_disconnect(pc_controller_t *);
pc_action_t pc_controller_key(pc_controller_t *, unsigned key, bool long_press, uint32_t now);
void pc_controller_ack(pc_controller_t *, const pc_message_t *);
bool pc_controller_update(pc_controller_t *, const pc_message_t *, uint32_t now);
bool pc_controller_tick(pc_controller_t *, uint32_t now);
void pc_controller_stop(pc_controller_t *);
uint32_t pc_timer_seconds(const pc_timer_t *);
bool pc_weather_stale(const pc_controller_t *);
typedef enum { PC_KEY_IGNORED, PC_KEY_CLICK, PC_KEY_HOLD } pc_key_intent_t;
pc_key_intent_t pc_key_guard(bool *suppress_ok, unsigned key, bool pressed, bool held);
