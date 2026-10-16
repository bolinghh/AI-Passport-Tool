#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "runtime.h"
#include <string.h>

static QueueHandle_t events;
static pc_parser_t parser;
static pc_controller_t controller;
static bool audio_ok, pairing;
static uint32_t passkey, active_generation;
static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
bool pc_post(const pc_event_t *event) { return events && xQueueSend(events, event, 0) == pdTRUE; }
static void button(bsp_btn_t key, bsp_btn_ev_t ev, void *ctx) {
    (void)ctx;
    pc_event_t e = {.type = PC_EVENT_KEY, .key = (uint8_t)key, .event = (uint8_t)ev};
    if (!pc_post(&e)) { /* Never block the shared button timer. */
    }
}
static void send_action(pc_action_t a) {
    if (!a.type)
        return;
    pc_message_t m = {
        .type = a.type, .request = a.request, .stream = a.stream, .length = 1, .payload = &a.value};
    if (!pc_link_send(&m, false)) {
        pc_capture_stream(0);
        pc_controller_disconnect(&controller);
        pc_link_drop();
    }
}
static bool greeted;
static uint32_t negotiation_at;
static void receive(const pc_message_t *m, void *ctx) {
    (void)ctx;
    if (m->type == PC_HELLO && !m->length) {
        greeted = true; controller.ready = false; negotiation_at = now_ms();
        pc_capture_stream(0); pc_controller_stop(&controller);
        pc_link_send(m, false); return;
    }
    if (greeted && pc_capabilities(m)) {
        controller.ready = true; controller.last_rx = now_ms();
        controller.hint = PC_HINT_NONE; pairing = false; pc_link_send(m, false); return;
    }
    if (m->type >= 2 && m->type <= 4) {
        controller.hint = PC_HINT_UPGRADE; pc_capture_stream(0);
        pc_controller_disconnect(&controller); controller.hint = PC_HINT_UPGRADE;
        pc_link_drop(); return;
    }
    if (!controller.ready) return;
    switch (m->type) {
    case PC_MIC_ACK: pc_controller_ack(&controller, m); break;
    case PC_TIME:
    case PC_WEATHER:
    case PC_SETTINGS_UPDATE: {
        bool ok = pc_controller_update(&controller, m, now_ms());
        if (m->type == PC_SETTINGS_UPDATE) {
            uint8_t result = ok ? 0 : 1;
            pc_message_t ack = {.type=PC_SETTINGS_ACK, .request=m->request, .length=1, .payload=&result};
            pc_link_send(&ack, false);
        }
        if (!ok) return;
        break;
    }
    case PC_HEARTBEAT: if (m->length) return; break;
    case PC_STOP: {
        if(m->length) return;
        pc_capture_stream(0); pc_controller_stop(&controller);
        pc_message_t ack={.type=PC_STOP_ACK,.request=m->request};pc_link_send(&ack,false);break;
    }
    case PC_ERROR: pc_capture_stream(0); pc_controller_stop(&controller); break;
    default: return;
    }
    controller.last_rx = now_ms();
}
static void telemetry(int battery) {
    int rssi = pc_link_rssi();
    uint8_t info[] = {(uint8_t)(int8_t)battery, (uint8_t)(int8_t)rssi,
                      (uint8_t)controller.page, (uint8_t)controller.mic};
    pc_message_t m = {.type=PC_DEVICE_STATUS, .length=sizeof(info), .payload=info};
    pc_link_send(&m, false);
    uint8_t timer[10] = {(uint8_t)controller.timer.rest, (uint8_t)controller.timer.state};
    pc_put32(timer+2, pc_timer_seconds(&controller.timer));
    pc_put32(timer+6, controller.timer.completion);
    m = (pc_message_t){.type=PC_TIMER_STATUS, .length=sizeof(timer), .payload=timer};
    pc_link_send(&m, false);
}
void app_main(void) {
    pc_controller_init(&controller);
    events = xQueueCreate(24, sizeof(pc_event_t));
    ESP_ERROR_CHECK(events ? ESP_OK : ESP_ERR_NO_MEM);
    /* Never erase NVS to hide an incompatible partition or initialization error. */
    ESP_ERROR_CHECK(nvs_flash_init());
    pc_play_init(&controller.play,esp_random());
    pc_play_store_init(&controller.play);
    ESP_ERROR_CHECK(bsp_i2c_init());
    ESP_ERROR_CHECK(bsp_display_init());
    ESP_ERROR_CHECK(bsp_lvgl_init() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(pc_screen_init());
    nvs_handle_t prefs;
    if (nvs_open("passport", NVS_READWRITE, &prefs) == ESP_OK) {
        uint8_t saved = 1;
        if (nvs_get_u8(prefs, "brightness", &saved) == ESP_OK && saved < 3) controller.brightness = saved;
        nvs_close(prefs);
    }
    bsp_display_backlight((uint8_t[]){25,70,100}[controller.brightness]);
    ESP_ERROR_CHECK(bsp_button_init(button, NULL));
    audio_ok = bsp_audio_init() == ESP_OK;
    if (audio_ok) {
        bsp_audio_set_volume(0);
        audio_ok = pc_capture_init() == ESP_OK;
        bsp_audio_sleep();
    }
    bsp_battery_init();
    ESP_ERROR_CHECK(pc_link_init());
    uint32_t last_heartbeat = 0, last_render = 0, last_battery = 0, last_stats = 0;
    int battery = -1;
    uint32_t last_telemetry = 0;
    bool consumed = false;
    for (;;) {
        pc_event_t event;
        uint32_t now = now_ms();
        if (xQueueReceive(events, &event, pdMS_TO_TICKS(20)) == pdTRUE) {
            if (event.type == PC_EVENT_CONNECTED || event.type == PC_EVENT_DISCONNECTED) {
                if (event.generation != pc_link_generation())
                    continue;
                active_generation = event.generation;
                parser.used = 0;
                pairing = false; greeted = false;
                pc_controller_disconnect(&controller);
                pc_capture_stream(0);
            } else if (event.type == PC_EVENT_KEY && !pairing) {
                pc_key_intent_t intent =
                    pc_key_guard(&consumed, event.key, event.event == BSP_BTN_PRESS,
                                 event.event == BSP_BTN_LONG);
                if (event.event == BSP_BTN_LONG && intent == PC_KEY_HOLD) {
                    send_action(pc_controller_key(&controller, 2, true, now));
                } else if (event.event == BSP_BTN_CLICK && intent == PC_KEY_CLICK) {
                    if (event.key == BSP_BTN_OK)
                        controller.suppress_click = false;
                    if (controller.page == PC_MICROPHONE && event.key == BSP_BTN_OK && !audio_ok) {
                        controller.hint = PC_HINT_AUDIO_FAILED;
                    } else if (controller.page == PC_SETTINGS && controller.confirm && !controller.timer.alert && event.key == BSP_BTN_OK) {
                        pc_link_forget(); controller.confirm = false;
                    } else {
                        uint8_t brightness = controller.brightness;
                        send_action(pc_controller_key(&controller, event.key, false, now));
                        if (controller.brightness != brightness) {
                            bsp_display_backlight((uint8_t[]){25,70,100}[controller.brightness]);
                            if (nvs_open("passport", NVS_READWRITE, &prefs) == ESP_OK) {
                                nvs_set_u8(prefs,"brightness",controller.brightness); nvs_commit(prefs); nvs_close(prefs);
                            }
                        }
                    }
                }
            } else if (event.generation == active_generation) {
                if (event.type == PC_EVENT_BYTES)
                    pc_feed(&parser, event.data, event.length, receive, NULL);
                else if (event.type == PC_EVENT_PAIR) {
                    passkey = event.value;
                    pairing = true;consumed = true;
                    pc_fishing_cancel(&controller.play);
                } else if (event.type == PC_EVENT_AUDIO_ERROR) {
                    controller.mic = PC_OFF;
                    controller.stream = 0;
                    controller.pending = 0;
                    controller.hint = PC_HINT_AUDIO_FAILED;
                    pc_message_t m = {.type = PC_ERROR, .stream = event.value};
                    pc_link_send(&m, false);
                }
            }
        }
        now = now_ms();
        if (greeted && !controller.ready && now - negotiation_at >= PC_TIMEOUT_MS) {
            greeted = false; controller.hint = PC_HINT_UPGRADE; pc_link_drop();
        }
        if (pc_controller_tick(&controller, now)) {
            pc_capture_stream(0);
            pc_link_drop();
            parser.used = 0;
        }
        pc_capture_stream(controller.mic == PC_LIVE ? controller.stream : 0);
        pc_play_store_tick(&controller.play,now,controller.mic==PC_OFF &&
                           controller.play.fishing!=PC_FISH_WAIT && controller.play.fishing!=PC_FISH_BITE);
        if (controller.ready && now - last_heartbeat >= PC_HEARTBEAT_MS) {
            pc_message_t h = {.type = PC_HEARTBEAT};
            pc_link_send(&h, false);
            last_heartbeat = now;
        }
        if (controller.ready && now - last_telemetry >= 1000) {
            telemetry(battery); last_telemetry = now;
        }
        if (now - last_battery >= 5000) {
            battery = bsp_battery_soc();
            pc_link_sample_rssi();
            last_battery = now;
        }
        if (now - last_render >= 150) {
            pc_screen_render(&controller, passkey, pairing, battery, pc_capture_level(), audio_ok,
                             controller.setting);
            last_render = now;
        }
        if (now - last_stats >= 10000) {
            ESP_LOGI("controller",
                     "heap=%u largest=%u main_stack=%u audio_stack=%u ble_tx_stack=%u",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                     (unsigned)uxTaskGetStackHighWaterMark(NULL), pc_capture_stack_margin(),
                     pc_link_stack_margin());
            last_stats = now;
        }
    }
}
