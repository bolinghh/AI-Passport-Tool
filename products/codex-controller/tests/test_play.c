#include "controller.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void pet(void) {
    pc_play_t p;pc_play_init(&p,0);assert(p.random && p.progress.treats==3 && pc_pet_level(&p.progress)==1);
    pc_pet_key(&p,2,0);assert(p.progress.treats==2 && p.progress.xp==5 && p.revision==1 && p.reaction==PC_PET_FED);
    pc_play_tick(&p,3000);assert(p.reaction==PC_PET_QUIET);
    pc_pet_key(&p,0,3001);assert(p.pet_action==2);pc_pet_key(&p,1,3002);assert(!p.pet_action);
    p.progress.treats=0;uint32_t revision=p.revision;
    pc_pet_key(&p,2,4000);assert(p.revision==revision && p.reaction==PC_PET_NO_FOOD);
    p.pet_action=1;pc_pet_key(&p,2,4001);assert(p.reaction==PC_PET_HAPPY && p.revision==revision);
    for(unsigned i=0;i<5;i++) pc_play_focus_completed(&p,5000+i);
    assert(pc_pet_level(&p.progress)==2 && p.progress.treats==10);
    p.pet_action=2;pc_pet_key(&p,2,6000);assert(p.progress.outfit==1);
    pc_pet_key(&p,2,6001);assert(!p.progress.outfit);
    p.progress.xp=300;assert(pc_pet_outfits(&p.progress)==3);
    pc_pet_key(&p,2,6002);pc_pet_key(&p,2,6003);assert(p.progress.outfit==2);
    p.progress.xp=99998;p.progress.treats=999;pc_play_focus_completed(&p,7000);
    assert(p.progress.xp==99999 && p.progress.treats==999 && pc_pet_level(&p.progress)==10);
}
static void fishing(void) {
    pc_play_t p;pc_play_init(&p,1234);pc_fishing_key(&p,2,100);
    assert(p.fishing==PC_FISH_WAIT && p.wait_ms>=2000 && p.wait_ms<=5000 && p.species<PC_FISH_COUNT);
    uint32_t random=p.random;pc_fishing_key(&p,0,101);assert(p.random==random && p.fishing==PC_FISH_WAIT);
    pc_fishing_key(&p,2,102);assert(p.fishing==PC_FISH_MISSED && p.early && !p.revision);
    pc_fishing_key(&p,2,1000);uint32_t bite=1000+p.wait_ms;
    pc_play_tick(&p,bite-1);assert(p.fishing==PC_FISH_WAIT);
    pc_fishing_key(&p,2,bite);assert(p.fishing==PC_FISH_CAUGHT);
    assert(p.progress.fish[p.species]==1 && p.progress.treats==4 && p.progress.xp==8 && pc_fish_discovered(&p.progress)==1);
    uint32_t revision=p.revision;pc_play_tick(&p,bite+5000);assert(p.revision==revision);
    pc_fishing_key(&p,1,bite+5001);assert(p.fishing==PC_FISH_BOOK);
    pc_fishing_key(&p,0,bite+5002);assert(p.book==PC_FISH_COUNT-1);
    pc_fishing_key(&p,1,bite+5003);assert(p.book==0);
    pc_fishing_key(&p,2,bite+5004);assert(p.fishing==PC_FISH_IDLE);
    pc_fishing_key(&p,2,100000);bite=100000+p.wait_ms;
    pc_fishing_key(&p,2,bite+PC_FISH_BITE_MS);assert(p.fishing==PC_FISH_MISSED); // Deadline taps never reward or silently recast.
    assert(p.revision==revision);
    pc_play_tick(&p,p.cast_at+p.wait_ms+PC_FISH_BITE_MS);assert(p.fishing==PC_FISH_MISSED && !p.early);
    pc_fishing_key(&p,2,UINT32_MAX-100);bite=(UINT32_MAX-100)+p.wait_ms;
    pc_play_tick(&p,bite);assert(p.fishing==PC_FISH_BITE);
    pc_fishing_key(&p,2,bite+PC_FISH_BITE_MS-1);assert(p.fishing==PC_FISH_CAUGHT);
    pc_fishing_key(&p,2,50000);pc_fishing_cancel(&p);pc_play_tick(&p,80000);assert(p.fishing==PC_FISH_IDLE);
    /* Fixed seed covers every rarity with bounded time and no platform RNG. */
    pc_play_init(&p,42);
    for(uint32_t i=0;i<1000;i++) {uint32_t at=i*10000;pc_fishing_key(&p,2,at);pc_fishing_key(&p,2,at+p.wait_ms);}
    assert(pc_fish_discovered(&p.progress)==PC_FISH_COUNT);
}
static void save_roundtrip(void) {
    pc_play_t p;pc_play_init(&p,1);p.progress.xp=400;p.progress.outfit=2;
    for(unsigned i=0;i<PC_FISH_COUNT;i++) p.progress.fish[i]=(uint16_t)(i*10);
    uint8_t data[PC_PLAY_SAVE_SIZE];pc_progress_encode(&p.progress,data);
    pc_progress_t restored={0};assert(pc_progress_decode(&restored,data,sizeof(data)));
    assert(restored.xp==400 && restored.outfit==2 && restored.treats==3 && restored.fish[5]==50);
    /* Every one-bit corruption and truncated blob must leave the output intact. */
    for(unsigned i=0;i<sizeof(data);i++) for(unsigned bit=0;bit<8;bit++) {
        data[i]^=(uint8_t)(1U<<bit);assert(!pc_progress_decode(&restored,data,sizeof(data)));
        assert(restored.xp==400 && restored.outfit==2);data[i]^=(uint8_t)(1U<<bit);
    }
    for(unsigned size=0;size<sizeof(data);size++) assert(!pc_progress_decode(&restored,data,size));
    p.progress.outfit=3;pc_progress_encode(&p.progress,data);assert(!pc_progress_decode(&restored,data,sizeof(data)));
    p.progress.outfit=2;p.progress.xp=0;pc_progress_encode(&p.progress,data);assert(!pc_progress_decode(&restored,data,sizeof(data)));
}
static void integration(void) {
    pc_controller_t s;pc_controller_init(&s);pc_controller_key(&s,2,false,0);
    const pc_page_t pages[]={PC_POMODORO,PC_MICROPHONE,PC_PET,PC_FISHING,PC_SETTINGS};
    for(unsigned i=0;i<PC_APP_COUNT;i++) {assert(pc_app_page(i)==pages[i]);assert(pc_app_window(i)<=i && i<pc_app_window(i)+3);}
    for(unsigned i=0;i<2;i++) pc_controller_key(&s,1,false,1);
    pc_controller_key(&s,2,false,2);assert(s.page==PC_PET);
    pc_controller_key(&s,2,true,3);pc_controller_key(&s,2,false,4);assert(s.page==PC_APPS && s.app==PC_APP_PET);
    pc_controller_key(&s,1,false,5);pc_controller_key(&s,2,false,6);assert(s.page==PC_FISHING);
    pc_controller_key(&s,2,false,7);assert(s.play.fishing==PC_FISH_WAIT);
    pc_controller_key(&s,2,true,8);assert(s.page==PC_APPS && s.play.fishing==PC_FISH_IDLE);
    pc_controller_key(&s,2,false,9); // Consume the held-button release.
    s.page=PC_POMODORO;s.timer.remaining_ms=1000;pc_controller_key(&s,2,false,100);
    s.page=PC_FISHING;pc_controller_key(&s,2,false,200);pc_controller_disconnect(&s);
    pc_controller_tick(&s,1100);assert(s.timer.alert && s.play.progress.xp==20 && s.play.progress.treats==5);
    assert(s.play.fishing==PC_FISH_IDLE);pc_controller_tick(&s,1200);assert(s.play.progress.xp==20);
    pc_controller_key(&s,0,false,1201);assert(s.play.fishing==PC_FISH_IDLE && s.timer.alert);
    pc_controller_key(&s,2,false,1202);assert(!s.timer.alert && s.play.fishing==PC_FISH_IDLE);
    s.timer.rest=true;s.timer.state=PC_TIMER_RUNNING;s.timer.remaining_ms=1;s.timer.tick_at=1300;
    pc_controller_tick(&s,1301);assert(s.play.progress.xp==20); // Rest never grants focus rewards.
    pc_controller_init(&s);assert(!s.play.progress.xp && s.timer.state==PC_TIMER_IDLE);
}
int main(void) {pet();fishing();save_roundtrip();integration();puts("Pet, fishing, progress serialization and navigation: PASS");return 0;}
