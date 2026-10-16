/* One bounded storage worker. It owns no LVGL objects and never runs in the
 * button callback. New progress snapshots replace queued older snapshots. */
#include "runtime.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdatomic.h>

static QueueHandle_t saves;
static _Atomic uint32_t saved_revision;
static _Atomic bool save_error;
static uint32_t last_submit;
typedef struct {uint32_t revision;uint8_t data[PC_PLAY_SAVE_SIZE];} save_t;
static void worker(void *unused) {
    (void)unused;save_t item;
    for(;;) if(xQueueReceive(saves,&item,portMAX_DELAY)==pdTRUE) {
        nvs_handle_t handle;
        esp_err_t err=nvs_open("passport",NVS_READWRITE,&handle);
        if(err==ESP_OK) {
            err=nvs_set_blob(handle,"play_v1",item.data,sizeof(item.data));
            if(err==ESP_OK) err=nvs_commit(handle);
            nvs_close(handle);
        }
        atomic_store(&save_error,err!=ESP_OK);
        if(err==ESP_OK) atomic_store(&saved_revision,item.revision);
        else ESP_LOGW("play_store","Progress save failed: %s",esp_err_to_name(err));
    }
}
void pc_play_store_init(pc_play_t *p) {
    nvs_handle_t handle;esp_err_t err=nvs_open("passport",NVS_READWRITE,&handle);
    if(err==ESP_OK) {
        uint8_t data[PC_PLAY_SAVE_SIZE];size_t size=sizeof(data);
        err=nvs_get_blob(handle,"play_v1",data,&size);nvs_close(handle);
        if(err==ESP_OK && !pc_progress_decode(&p->progress,data,size)) err=ESP_ERR_INVALID_STATE;
        else if(err==ESP_ERR_NVS_NOT_FOUND) err=ESP_OK;
    }
    /* Preserve unknown or corrupt stored data for recovery; do not overwrite it
     * with a fresh pet. The apps remain playable with an explicit save warning. */
    if(err==ESP_OK) {
        saves=xQueueCreate(1,sizeof(save_t));
        if(!saves) err=ESP_ERR_NO_MEM;
        else if(xTaskCreate(worker,"play_store",3072,NULL,1,NULL)!=pdPASS) {
            vQueueDelete(saves);saves=NULL;err=ESP_ERR_NO_MEM;
        }
    }
    if(err!=ESP_OK) {p->save_failed=true;ESP_LOGW("play_store","Progress store unavailable: %s",esp_err_to_name(err));}
}
void pc_play_store_tick(pc_play_t *p,uint32_t now,bool safe) {
    if(!saves) return;
    p->save_failed=atomic_load(&save_error);
    p->saving=p->revision!=atomic_load(&saved_revision);
    /* Coalesce interaction bursts. Catch results can save immediately so a
     * player who keeps recasting still checkpoints progress. Never enqueue in
     * live/preparing audio or an active cast, and cap writes at one per 3 s. */
    bool settled=p->fishing==PC_FISH_CAUGHT || now-p->changed_at>=1200;
    if(!p->saving || !safe || !settled || (last_submit && now-last_submit<3000)) return;
    save_t item={.revision=p->revision};pc_progress_encode(&p->progress,item.data);
    if(xQueueOverwrite(saves,&item)==pdTRUE) last_submit=now;
}
