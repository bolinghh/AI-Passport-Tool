/* Fault-inject the real store boundary without starting a host thread. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "../main/play_store.c"
static esp_err_t open_error,get_error,set_error,commit_error;
static bool queue_fail,task_fail,pending;
static unsigned creates,deletes,writes,commits;
static uint8_t disk[PC_PLAY_SAVE_SIZE];
static save_t queued;
static jmp_buf complete;
const char *esp_err_to_name(esp_err_t err) {(void)err;return "injected";}
esp_err_t nvs_open(const char *ns,int mode,nvs_handle_t *handle) {
    assert(!strcmp(ns,"passport") && mode==NVS_READWRITE);*handle=1;return open_error;
}
esp_err_t nvs_get_blob(nvs_handle_t handle,const char *key,void *out,size_t *size) {
    assert(handle==1 && !strcmp(key,"play_v1") && *size==PC_PLAY_SAVE_SIZE);
    if(!get_error) memcpy(out,disk,*size);return get_error;
}
esp_err_t nvs_set_blob(nvs_handle_t handle,const char *key,const void *in,size_t size) {
    assert(handle==1 && !strcmp(key,"play_v1") && size==PC_PLAY_SAVE_SIZE);writes++;
    if(!set_error) memcpy(disk,in,size);return set_error;
}
esp_err_t nvs_commit(nvs_handle_t handle) {assert(handle==1);commits++;return commit_error;}
void nvs_close(nvs_handle_t handle) {assert(handle==1);}
QueueHandle_t xQueueCreate(unsigned count,size_t size) {assert(count==1 && size==sizeof(save_t));creates++;return queue_fail?NULL:&queued;}
void vQueueDelete(QueueHandle_t q) {assert(q==&queued);deletes++;}
int xTaskCreate(void (*task)(void *),const char *name,unsigned stack,void *context,unsigned priority,void *handle) {
    assert(task==worker && name && stack==3072 && !context && priority==1 && !handle);return task_fail?0:pdPASS;
}
int xQueueOverwrite(QueueHandle_t q,const void *in) {assert(q==&queued);memcpy(&queued,in,sizeof(queued));pending=true;return pdTRUE;}
int xQueueReceive(QueueHandle_t q,void *out,unsigned timeout) {
    assert(q==&queued && timeout==portMAX_DELAY);
    if(!pending) longjmp(complete,1);
    memcpy(out,&queued,sizeof(queued));pending=false;return pdTRUE;
}
static void run_worker(void) {if(!setjmp(complete)) worker(NULL);}
static pc_play_t reset(void) {
    saves=NULL;atomic_store(&saved_revision,0);atomic_store(&save_error,false);last_submit=0;
    open_error=set_error=commit_error=0;get_error=ESP_ERR_NVS_NOT_FOUND;
    creates=deletes=writes=commits=0;queue_fail=task_fail=pending=false;
    pc_play_t p;pc_play_init(&p,1);return p;
}
int main(void) {
    pc_play_t p=reset();pc_play_store_init(&p);assert(saves && !p.save_failed && !writes);
    pc_pet_key(&p,2,10);pc_play_store_tick(&p,1000,true);assert(!pending && p.saving);
    pc_play_store_tick(&p,4000,false);assert(!pending);
    pc_play_store_tick(&p,4000,true);assert(pending && queued.revision==p.revision);
    run_worker();pc_play_store_tick(&p,4001,true);assert(!p.saving && !p.save_failed && writes==1 && commits==1);
    pc_progress_t loaded={0};assert(pc_progress_decode(&loaded,disk,sizeof(disk)) && loaded.treats==2);
    pc_pet_key(&p,2,5000);commit_error=ESP_ERR_NO_MEM;
    pc_play_store_tick(&p,8000,true);run_worker();pc_play_store_tick(&p,8001,true);
    assert(p.save_failed && p.saving && atomic_load(&saved_revision)!=p.revision);
    commit_error=0;pc_play_store_tick(&p,11000,true);run_worker();pc_play_store_tick(&p,11001,true);
    assert(!p.save_failed && !p.saving);
    /* A slow worker must commit the latest coalesced snapshot. */
    pc_play_focus_completed(&p,12000);pc_play_store_tick(&p,15000,true);
    pc_play_focus_completed(&p,16000);pc_play_store_tick(&p,19000,true);run_worker();
    assert(pc_progress_decode(&loaded,disk,sizeof(disk)) && loaded.xp==p.progress.xp);
    p=reset();get_error=0;pc_play_store_init(&p);assert(p.progress.xp==loaded.xp && !p.save_failed && !p.revision);
    p=reset();pc_play_store_init(&p);
    pc_play_focus_completed(&p,2000);p.fishing=PC_FISH_CAUGHT;
    pc_play_store_tick(&p,2000,true);assert(pending);run_worker();
    pc_play_focus_completed(&p,4000);pc_play_store_tick(&p,4000,true);assert(!pending);
    pc_play_store_tick(&p,5000,true);assert(pending);run_worker();
    p=reset();disk[0]^=1;get_error=0;pc_play_store_init(&p);assert(p.save_failed && !saves && !creates && !writes);
    p=reset();open_error=ESP_ERR_NO_MEM;pc_play_store_init(&p);assert(p.save_failed && !creates);
    p=reset();queue_fail=true;pc_play_store_init(&p);assert(p.save_failed && !saves);
    p=reset();task_fail=true;pc_play_store_init(&p);assert(p.save_failed && !saves && deletes==1);
    puts("Progress store: PASS (load, corruption protection, async coalescing, failure/retry and allocation cleanup)");
}
