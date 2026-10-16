#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "opus.h"
#include "runtime.h"
#include <stdatomic.h>
#include <stdlib.h>

static atomic_uint wanted_stream, level, failed_stream;
static OpusEncoder *encoder;
static StaticTask_t worker_storage;
static TaskHandle_t worker_handle;
static StackType_t worker_stack[24576 / sizeof(StackType_t)];
static int16_t pcm[960];
static uint8_t packet[PC_AUDIO_MAX];

void pc_capture_stream(uint32_t stream) {
    atomic_store(&wanted_stream, stream);
    if (!stream)
        atomic_store(&level, 0);
}
unsigned pc_capture_level(void) { return atomic_load(&level); }
unsigned pc_capture_stack_margin(void) {
    return worker_handle ? (unsigned)uxTaskGetStackHighWaterMark(worker_handle) : 0;
}
static void failed(uint32_t stream) {
    atomic_store(&failed_stream, stream);
    pc_capture_stream(0);
    pc_event_t e = {
        .type = PC_EVENT_AUDIO_ERROR, .generation = pc_link_generation(), .value = stream};
    if (!pc_post(&e))
        pc_link_drop();
}
static void capture_task(void *arg) {
    (void)arg;
    uint32_t active = 0;
    uint16_t seq = 0;
    for (;;) {
        uint32_t desired = pc_link_ready() ? atomic_load(&wanted_stream) : 0;
        if (desired == atomic_load(&failed_stream))
            desired = 0;
        if (desired != active) {
            if (active) {
                bsp_audio_sleep();
                active = 0;
            }
            if (desired) {
                if (bsp_audio_wake() != ESP_OK || bsp_audio_set_format(16000, 16, 1) != ESP_OK) {
                    bsp_audio_sleep();
                    failed(desired);
                    continue;
                }
                bsp_audio_set_volume(0);
                opus_encoder_ctl(encoder, OPUS_RESET_STATE);
                active = desired;
                seq = 0;
            }
        }
        if (!active) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        bool ok = true;
        /* 20 ms chunks bound normal cancellation latency; no long recording buffer.
         */
        for (unsigned at = 0; at < 960; at += 320) {
            if (atomic_load(&wanted_stream) != active) {
                ok = false;
                break;
            }
            if (bsp_audio_read(pcm + at, 320 * sizeof(int16_t)) != ESP_OK) {
                failed(active);
                ok = false;
                break;
            }
        }
        if (!ok || atomic_load(&wanted_stream) != active)
            continue;
        unsigned peak = 0;
        for (unsigned i = 0; i < 960; i++) {
            int v = pcm[i];
            unsigned a = (unsigned)(v < 0 ? -v : v);
            if (a > peak)
                peak = a;
        }
        atomic_store(&level, peak * 100 / 32768);
        int n = opus_encode(encoder, pcm, 960, packet, sizeof(packet));
        if (n <= 0) {
            failed(active);
            continue;
        }
        pc_message_t m = {.type = PC_AUDIO,
                          .stream = active,
                          .sequence = seq++,
                          .length = (uint16_t)n,
                          .payload = packet};
        /* Whole-frame queue overflow drops this frame; the decoder can conceal it.
         */
        pc_link_send(&m, true);
    }
}
esp_err_t pc_capture_init(void) {
    int error = OPUS_OK;
    ESP_LOGI("controller_audio", "encoder allocation: %d bytes", opus_encoder_get_size(1));
    encoder = opus_encoder_create(16000, 1, OPUS_APPLICATION_VOIP, &error);
    if (!encoder || error != OPUS_OK) {
        if (encoder)
            opus_encoder_destroy(encoder);
        encoder = NULL;
        return ESP_ERR_NO_MEM;
    }
    if (opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(0)) != OPUS_OK ||
        opus_encoder_ctl(encoder, OPUS_SET_BITRATE(16000)) != OPUS_OK ||
        opus_encoder_ctl(encoder, OPUS_SET_VBR(0)) != OPUS_OK ||
        opus_encoder_ctl(encoder, OPUS_SET_DTX(0)) != OPUS_OK) {
        opus_encoder_destroy(encoder);
        encoder = NULL;
        return ESP_FAIL;
    }
    worker_handle = xTaskCreateStatic(capture_task, "controller_audio", sizeof(worker_stack), NULL,
                                      6, worker_stack, &worker_storage);
    if (!worker_handle) {
        opus_encoder_destroy(encoder);
        encoder = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
