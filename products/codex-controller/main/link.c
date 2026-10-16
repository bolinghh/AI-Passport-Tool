#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "runtime.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <stdatomic.h>
#include <string.h>

static const char *TAG = "controller_ble";
static const ble_uuid128_t service_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
static const ble_uuid128_t rx_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
static const ble_uuid128_t tx_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);
static atomic_uint connection = BLE_HS_CONN_HANDLE_NONE, generation, secure, subscribed;
static uint16_t tx_handle;
static uint8_t own_addr_type;
static struct ble_npl_callout adv_retry;
static struct ble_npl_event drop_event, forget_event;
static bool forget_requested;
static TaskHandle_t host_handle, transmit_handle;
typedef struct {
    uint32_t generation;
    uint16_t length;
    /* Control and audio frames are bounded independently. */
    uint8_t bytes[PC_TX_MAX];
} tx_t;
static QueueHandle_t control_queue, audio_queue;
static StaticQueue_t control_storage, audio_storage;
static uint8_t control_buffer[4 * sizeof(tx_t)], audio_buffer[4 * sizeof(tx_t)];

static atomic_int cached_rssi = 127;
int pc_link_rssi(void) { return atomic_load(&cached_rssi); }
void pc_link_sample_rssi(void) {
    int8_t rssi;
    uint16_t conn = (uint16_t)atomic_load(&connection);
    atomic_store(&cached_rssi, conn != BLE_HS_CONN_HANDLE_NONE && ble_gap_conn_rssi(conn, &rssi) == 0 ? rssi : 127);
}
uint32_t pc_link_generation(void) { return atomic_load(&generation); }
bool pc_link_ready(void) { return atomic_load(&secure) && atomic_load(&subscribed); }
unsigned pc_link_stack_margin(void) {
    unsigned host = host_handle ? (unsigned)uxTaskGetStackHighWaterMark(host_handle) : 0;
    unsigned tx = transmit_handle ? (unsigned)uxTaskGetStackHighWaterMark(transmit_handle) : 0;
    return host < tx ? host : tx;
}
static void notify_app(pc_event_type_t type, uint32_t value) {
    pc_event_t event = {.type = type, .generation = pc_link_generation(), .value = value};
    if (!pc_post(&event)) {
        /* A lost lifecycle event must never leave capture running. */
        pc_capture_stream(0);
        pc_link_drop();
    }
}
static int access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctx, void *arg) {
    (void)attr;
    (void)arg;
    if (!atomic_load(&secure) || conn != atomic_load(&connection))
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    if (ctx->op != BLE_GATT_ACCESS_OP_WRITE_CHR)
        return BLE_ATT_ERR_READ_NOT_PERMITTED;
    pc_event_t event = {.type = PC_EVENT_BYTES, .generation = pc_link_generation()};
    uint16_t length = OS_MBUF_PKTLEN(ctx->om);
    if (!length || length > sizeof(event.data))
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if (os_mbuf_copydata(ctx->om, 0, length, event.data))
        return BLE_ATT_ERR_UNLIKELY;
    event.length = length;
    return pc_post(&event) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}
static const struct ble_gatt_svc_def services[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &service_uuid.u,
     .characteristics =
         (struct ble_gatt_chr_def[]){
             {.uuid = &rx_uuid.u,
              .access_cb = access,
              .flags =
                  BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN},
             {.uuid = &tx_uuid.u,
              .access_cb = access,
              .val_handle = &tx_handle,
              .flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC},
             {0}}},
    {0}};
/* IDF 5.5.3 propagates CCCD write encryption but not read encryption.
 * Protect reads of that descriptor too; do not touch unrelated descriptors. */
typedef int pc_att_access_fn(uint16_t, uint16_t, uint8_t, uint16_t, struct os_mbuf **, void *);
int __real_ble_att_svr_register(const ble_uuid_t *, uint8_t, uint8_t, uint16_t *,
                                pc_att_access_fn *, void *);
int __wrap_ble_att_svr_register(const ble_uuid_t *uuid, uint8_t flags, uint8_t min,
                                uint16_t *handle, pc_att_access_fn *cb, void *arg) {
    if (ble_uuid_u16(uuid) == 0x2902 && (flags & BLE_ATT_F_WRITE_ENC))
        flags |= BLE_ATT_F_READ_ENC;
    return __real_ble_att_svr_register(uuid, flags, min, handle, cb, arg);
}
static int gap_event(struct ble_gap_event *, void *);
static void advertise(struct ble_npl_event *event) {
    (void)event;
    if (atomic_load(&connection) != BLE_HS_CONN_HANDLE_NONE || ble_gap_adv_active())
        return;
    struct ble_hs_adv_fields f = {0};
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.uuids128 = (ble_uuid128_t *)&service_uuid;
    f.num_uuids128 = 1;
    f.uuids128_is_complete = 1;
    struct ble_hs_adv_fields response = {0};
    const char *name = ble_svc_gap_device_name();
    response.name = (uint8_t *)name;
    response.name_len = (uint8_t)strlen(name);
    response.name_is_complete = 1;
    struct ble_gap_adv_params params = {.conn_mode = BLE_GAP_CONN_MODE_UND,
                                        .disc_mode = BLE_GAP_DISC_MODE_GEN};
    int rc = ble_gap_adv_set_fields(&f);
    if (!rc)
        rc = ble_gap_adv_rsp_set_fields(&response);
    if (!rc)
        rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc) {
        ESP_LOGW(TAG, "advertising retry: %d", rc);
        ble_npl_callout_reset(&adv_retry, ble_npl_time_ms_to_ticks32(1000));
    }
}
static void reset_connection(void) {
    atomic_store(&cached_rssi, 127);
    atomic_store(&secure, 0);
    atomic_store(&subscribed, 0);
    atomic_store(&connection, BLE_HS_CONN_HANDLE_NONE);
    atomic_fetch_add(&generation, 1);
    pc_capture_stream(0);
    xQueueReset(control_queue);
    xQueueReset(audio_queue);
}
static int gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "connect status=%d", event->connect.status);
        if (event->connect.status) {
            advertise(NULL);
            break;
        }
        atomic_fetch_add(&generation, 1);
        atomic_store(&connection, event->connect.conn_handle);
        atomic_store(&secure, 0);
        atomic_store(&subscribed, 0);
        notify_app(PC_EVENT_CONNECTED, 0);
        int security_rc = ble_gap_security_initiate(event->connect.conn_handle);
        ESP_LOGI(TAG, "security initiate rc=%d", security_rc);
        if (security_rc)
            pc_link_drop();
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect reason=%d", event->disconnect.reason);
        reset_connection();
        if (forget_requested) {
            forget_requested = false;
            int rc = ble_store_clear();
            if (rc)
                ESP_LOGE(TAG, "bond clear: %d", rc);
            notify_app(PC_EVENT_DISCONNECTED, (uint32_t)rc);
        } else
            notify_app(PC_EVENT_DISCONNECTED, 0);
        advertise(NULL);
        break;
    case BLE_GAP_EVENT_ENC_CHANGE: {
        struct ble_gap_conn_desc d = {0};
        int find_rc = ble_gap_conn_find(event->enc_change.conn_handle, &d);
        ESP_LOGI(TAG, "encryption status=%d lookup=%d encrypted=%u authenticated=%u bonded=%u",
                 event->enc_change.status, find_rc, d.sec_state.encrypted,
                 d.sec_state.authenticated, d.sec_state.bonded);
        if (!event->enc_change.status && !find_rc &&
            d.sec_state.encrypted && d.sec_state.authenticated && d.sec_state.bonded) {
            atomic_store(&secure, 1);
            if (atomic_load(&subscribed))
                notify_app(PC_EVENT_CONNECTED, 0);
        } else
            pc_link_drop();
        break;
    }
    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "subscribe handle=%u previous=%u current=%u reason=%u",
                 event->subscribe.attr_handle, event->subscribe.prev_notify,
                 event->subscribe.cur_notify, event->subscribe.reason);
        if (event->subscribe.attr_handle == tx_handle) {
            atomic_store(&subscribed, event->subscribe.cur_notify);
            if (event->subscribe.cur_notify && atomic_load(&secure))
                notify_app(PC_EVENT_CONNECTED, 0);
            if (pc_subscription_lost(event->subscribe.prev_notify,
                                     event->subscribe.cur_notify))
                pc_link_drop();
        }
        break;
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        ESP_LOGI(TAG, "passkey action=%u", event->passkey.params.action);
        if (event->passkey.params.action != BLE_SM_IOACT_DISP)
            return BLE_HS_ENOTSUP;
        uint32_t key = 100000 + esp_random() % 900000;
        notify_app(PC_EVENT_PAIR, key);
        struct ble_sm_io io = {.action = BLE_SM_IOACT_DISP, .passkey = key};
        return ble_sm_inject_io(event->passkey.conn_handle, &io);
    }
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        ESP_LOGW(TAG, "repeat pairing rejected; explicit device/OS bond reset required");
        /* Do not silently delete a trusted desktop's bond. Reset is explicit. */
        pc_link_drop();
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    case BLE_GAP_EVENT_PARING_COMPLETE:
        ESP_LOGI(TAG, "pairing complete status=%d", event->pairing_complete.status);
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise(NULL);
        break;
    default:
        break;
    }
    return 0;
}
static void drop(struct ble_npl_event *event) {
    (void)event;
    uint16_t conn = (uint16_t)atomic_load(&connection);
    if (conn != BLE_HS_CONN_HANDLE_NONE) {
        int rc = ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
        if (rc && rc != BLE_HS_ENOTCONN) {
            ESP_LOGE(TAG, "terminate: %d", rc);
            ble_hs_sched_reset(rc);
        }
    }
}
void pc_link_drop(void) {
    atomic_store(&secure, 0);
    pc_capture_stream(0);
    if (!ble_npl_event_is_queued(&drop_event))
        ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &drop_event);
}
static void forget(struct ble_npl_event *event) {
    (void)event;
    /* Prevent reconnection between disconnect and clearing the persistent bond. */
    if (ble_gap_adv_active())
        ble_gap_adv_stop();
    if (atomic_load(&connection) != BLE_HS_CONN_HANDLE_NONE) {
        forget_requested = true;
        drop(NULL);
        return;
    }
    int rc = ble_store_clear();
    if (rc)
        ESP_LOGE(TAG, "bond clear: %d", rc);
    notify_app(PC_EVENT_DISCONNECTED, (uint32_t)rc);
    advertise(NULL);
}
esp_err_t pc_link_forget(void) {
    pc_capture_stream(0);
    atomic_store(&secure, 0);
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &forget_event);
    return ESP_OK;
}
static void host_sync(void) {
    if (ble_hs_util_ensure_addr(0) || ble_hs_id_infer_auto(0, &own_addr_type)) {
        ESP_LOGE(TAG, "BLE address unavailable");
        return;
    }
    advertise(NULL);
}
static void host_reset(int reason) {
    ESP_LOGW(TAG, "host reset: %d", reason);
    reset_connection();
    notify_app(PC_EVENT_DISCONNECTED, 0);
}
static void host_task(void *arg) {
    (void)arg;
    nimble_port_run();
    vTaskDelete(NULL);
}
static void transmit(void *arg) {
    (void)arg;
    static tx_t frame;
    for (;;) {
        if (xQueueReceive(control_queue, &frame, 0) != pdTRUE &&
            xQueueReceive(audio_queue, &frame, pdMS_TO_TICKS(10)) != pdTRUE)
            continue;
        size_t at = 0;
        while (at < frame.length && frame.generation == pc_link_generation() &&
               atomic_load(&secure) && atomic_load(&subscribed)) {
            uint16_t conn = (uint16_t)atomic_load(&connection);
            uint16_t mtu = ble_att_mtu(conn);
            size_t n = mtu > 3 ? mtu - 3 : 20;
            if (n > frame.length - at)
                n = frame.length - at;
            struct os_mbuf *mb = ble_hs_mbuf_from_flat(frame.bytes + at, (uint16_t)n);
            int rc = mb ? ble_gatts_notify_custom(conn, tx_handle, mb) : BLE_HS_ENOMEM;
            if (rc) { /* An incomplete frame requires resetting the byte stream. */
                ESP_LOGW(TAG, "notify: %d", rc);
                pc_link_drop();
                break;
            }
            at += n;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}
bool pc_link_send(const pc_message_t *m, bool audio) {
    if (!atomic_load(&secure) || !atomic_load(&subscribed))
        return false;
    /* Producers have separate frame storage; no heap allocation per message. */
    tx_t frame = {.generation = pc_link_generation()};
    frame.length = (uint16_t)pc_encode(frame.bytes, sizeof(frame.bytes), m);
    return frame.length && xQueueSend(audio ? audio_queue : control_queue, &frame, 0) == pdTRUE;
}
void ble_store_config_init(void);
esp_err_t pc_link_init(void) {
    control_queue = xQueueCreateStatic(4, sizeof(tx_t), control_buffer, &control_storage);
    audio_queue = xQueueCreateStatic(4, sizeof(tx_t), audio_buffer, &audio_storage);
    esp_err_t e = nimble_port_init();
    if (e != ESP_OK)
        return e;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("FAP-Passport");
    ble_hs_cfg.sync_cb = host_sync;
    ble_hs_cfg.reset_cb = host_reset;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_store_config_init();
    ble_npl_callout_init(&adv_retry, nimble_port_get_dflt_eventq(), advertise, NULL);
    ble_npl_event_init(&drop_event, drop, NULL);
    ble_npl_event_init(&forget_event, forget, NULL);
    if (ble_gatts_count_cfg(services) || ble_gatts_add_svcs(services))
        return ESP_FAIL;
    if (xTaskCreate(transmit, "controller_tx", 4096, NULL, 5, &transmit_handle) != pdPASS)
        return ESP_ERR_NO_MEM;
    /* Use checked task creation instead of the SDK helper's unchecked result. */
    if (xTaskCreate(host_task, "controller_ble", 4096, NULL, 5, &host_handle) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}
