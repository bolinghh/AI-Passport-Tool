/* Headless rendering of the actual firmware screen.c and LVGL font assets.
 * Never connects to Bluetooth/audio or opens a USB device. */
#include "lvgl.h"
#include "runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char framebuffer[240*320*2], buffer[240*40*2];
static int rssi=-61;
static unsigned frames,captures;
bool bsp_lvgl_lock(int ms) {(void)ms;return true;}
void bsp_lvgl_unlock(void) {}
int pc_link_rssi(void) {return rssi;}
static void flush(lv_display_t *d,const lv_area_t *area,uint8_t *pixels) {
    assert(area->x1>=0 && area->y1>=0 && area->x2<240 && area->y2<320);
    for(int y=area->y1;y<=area->y2;y++) {
        size_t count=(size_t)(area->x2-area->x1+1)*2;
        memcpy(framebuffer+((size_t)y*240+area->x1)*2,pixels,count);pixels+=count;
    }
    frames++;lv_display_flush_ready(d);
}
static uint32_t next(const char **p) {
    uint32_t value=(uint8_t)*(*p)++;
    if(value<128) return value;
    unsigned trailing=value<224?1:value<240?2:3;
    value&=(1U<<(6-trailing))-1;
    while(trailing--) value=(value<<6)|((uint8_t)*(*p)++&63);
    return value;
}
static void check_labels(lv_obj_t *o) {
    if(lv_obj_has_flag(o,LV_OBJ_FLAG_HIDDEN)) return;
    if(lv_obj_check_type(o,&lv_label_class)) {
        const char *p=lv_label_get_text(o);
        const lv_font_t *font=lv_obj_get_style_text_font(o,0);
        while(*p) {
            uint32_t code=next(&p);if(code=='\n') continue;
            lv_font_glyph_dsc_t glyph={0};
            if(!lv_font_get_glyph_dsc(font,&glyph,code,0) || glyph.is_placeholder) {
                fprintf(stderr,"Missing U+%04X in %s\n",code,lv_label_get_text(o));exit(1);
            }
        }
        lv_area_t box;lv_obj_get_coords(o,&box);
        assert(box.x1>=0 && box.y1>=0 && box.x2<240 && box.y2<320);
        lv_point_t size;
        lv_text_get_size(&size,lv_label_get_text(o),font,0,0,lv_obj_get_width(o),LV_TEXT_FLAG_NONE);
        if(size.y>lv_obj_get_height(o)) {
            fprintf(stderr,"Clipped label: %s (%d > %d)\n",lv_label_get_text(o),(int)size.y,(int)lv_obj_get_height(o));exit(1);
        }
    }
    for(uint32_t i=0;i<lv_obj_get_child_count(o);i++) check_labels(lv_obj_get_child(o,(int32_t)i));
}
static unsigned count_color(int x,int y,int w,int h,uint32_t color) {
    uint16_t expected=(uint16_t)(((color>>19)&31)<<11 | ((color>>10)&63)<<5 | ((color>>3)&31));
    unsigned count=0;
    for(int yy=y;yy<y+h;yy++) for(int xx=x;xx<x+w;xx++) {
        size_t at=((size_t)yy*240+xx)*2;
        uint16_t value=(uint16_t)(framebuffer[at]|framebuffer[at+1]<<8);
        count+=value==expected;
    }
    return count;
}
static void check_art(const pc_controller_t *s,bool pair) {
    /* A text-only render can pass glyph checks while dropping every sprite.
     * Check the actual framebuffer in the same 24 KB pool and partial mode. */
    if(s->ready) assert(count_color(14,12,20,20,0x55c8ed)>20);
    if(pair || s->confirm || s->timer.alert) {
        assert(count_color(14,72,212,196,0xffbd55)>500);return;
    }
    if(s->page==PC_HOME || s->page==PC_POMODORO) {
        assert(count_color(14,64,212,90,0xffedbd)>500);
    }
    if(s->page==PC_APPS) {
        assert(count_color(12,77,216,194,0xffbd55)>400);
        unsigned first=pc_app_window(s->app);
        for(unsigned slot=0;slot<3;slot++) {
            unsigned app=first+slot;int y=88+(int)slot*66;
            if(app==PC_APP_POMODORO) assert(count_color(39,y,36,40,0xff6346)>100);
            if(app==PC_APP_MICROPHONE) assert(count_color(39,y,36,40,0x5bd3a2)>50);
            if(app==PC_APP_PET) assert(count_color(39,y,36,40,0x79d9cf)>100);
            if(app==PC_APP_SETTINGS) assert(count_color(39,y,36,40,0xb5d3e5)>100);
        }
    }
    if(s->page==PC_PET) assert(count_color(80,90,90,90,0x79d9cf)>600);
    if(s->page==PC_FISHING && s->play.fishing!=PC_FISH_BOOK)
        assert(count_color(16,145,208,55,0x103c56)>3000);
    if(s->page==PC_HOME && s->weather_valid && s->weather_code==0)
        assert(count_color(50,170,144,65,0xffbd55)>400);
}
static void save(lv_display_t *d,const char *dir,const char *name,pc_controller_t *s,bool pair,int battery) {
    pc_screen_render(s,123456,pair,battery,68,true,s->setting);
    lv_obj_update_layout(lv_screen_active());lv_refr_now(d);check_labels(lv_screen_active());check_art(s,pair);
    char path[1024];snprintf(path,sizeof(path),"%s/%s.ppm",dir,name);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n240 320\n255\n");
    for(unsigned y=0;y<320;y++) for(unsigned x=0;x<240;x++) {
        unsigned off=(y*240+x)*2;uint16_t px=(uint16_t)(framebuffer[off]|framebuffer[off+1]<<8);
        unsigned r=(px>>11)&31,g=(px>>5)&63,b=px&31;
        unsigned char rgb[3]={(unsigned char)((r<<3)|(r>>2)),(unsigned char)((g<<2)|(g>>4)),(unsigned char)((b<<3)|(b>>2))};
        /* Match the BSP's 30 px physical corner mask. */
        int dx=x<30?30-(int)x:x>=210?(int)x-209:0;
        int dy=y<30?30-(int)y:y>=290?(int)y-289:0;
        if(dx*dx+dy*dy>900) memset(rgb,0,sizeof(rgb));
        fwrite(rgb,1,3,f);
    }
    fclose(f);captures++;
}
int main(int argc,char **argv) {
    assert(argc==2);lv_init();lv_display_t *d=lv_display_create(240,320);
    lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d,flush);assert(pc_screen_init()==ESP_OK);
    pc_controller_t s;pc_controller_init(&s);s.ready=true;s.clock_valid=true;
    s.epoch=1792028460;s.utc_offset=8*3600;s.weather_valid=true;s.weather_at=s.epoch;
    s.temperature=230;strcpy(s.city,"示例城市");
    save(d,argv[1],"home",&s,false,82);
    s.page=PC_APPS;for(unsigned i=0;i<PC_APP_COUNT;i++) {s.app=i;char name[24];snprintf(name,sizeof(name),"apps-%u",i);save(d,argv[1],name,&s,false,82);}
    s.page=PC_POMODORO;save(d,argv[1],"timer",&s,false,82);
    s.timer.remaining_ms=120*60000;save(d,argv[1],"timer-120",&s,false,82);
    s.confirm=true;save(d,argv[1],"reset",&s,false,82);s.confirm=false;
    s.page=PC_MICROPHONE;s.mic=PC_LIVE;save(d,argv[1],"microphone",&s,false,82);
    s.page=PC_SETTINGS;save(d,argv[1],"settings",&s,false,82);
    s.confirm=true;save(d,argv[1],"unpair",&s,false,82);s.confirm=false;
    s.timer.alert=true;save(d,argv[1],"alert",&s,false,82);s.timer.alert=false;
    save(d,argv[1],"pairing",&s,true,82);
    s.page=PC_HOME;uint16_t codes[]={1,3,45,61,73,95};
    for(unsigned i=0;i<sizeof(codes)/sizeof(codes[0]);i++) {s.weather_code=codes[i];char name[24];snprintf(name,sizeof(name),"weather-%u",codes[i]);save(d,argv[1],name,&s,false,82);}
    s.epoch+=7200;strcpy(s.city,"中华人民共和国示例城市");s.temperature=-123;
    save(d,argv[1],"stale-weather",&s,false,5);
    s.ready=false;s.clock_valid=false;s.weather_valid=false;rssi=127;
    save(d,argv[1],"offline",&s,false,-1);
    s.page=PC_PET;pc_play_init(&s.play,42);
    save(d,argv[1],"pet",&s,false,82);
    pc_pet_key(&s.play,2,1000);save(d,argv[1],"pet-fed",&s,false,82);
    pc_pet_key(&s.play,1,2000);pc_pet_key(&s.play,2,2000);save(d,argv[1],"pet-happy",&s,false,82);
    s.play.pet_action=0;s.play.progress.treats=0;pc_pet_key(&s.play,2,3000);save(d,argv[1],"pet-hungry",&s,false,82);
    s.play.pet_action=2;pc_pet_key(&s.play,2,4000);save(d,argv[1],"pet-locked",&s,false,82);
    s.play.progress.xp=120;pc_pet_key(&s.play,2,5000);save(d,argv[1],"pet-scarf",&s,false,82);
    s.play.progress.xp=325;pc_pet_key(&s.play,2,6000);save(d,argv[1],"pet-crown",&s,false,82);
    s.play.progress.xp=99999;pc_play_tick(&s.play,10000);save(d,argv[1],"pet-max",&s,false,82);
    s.play.save_failed=true;save(d,argv[1],"pet-save-error",&s,false,82);s.play.save_failed=false;
    s.play.saving=true;save(d,argv[1],"pet-saving",&s,false,82);s.play.saving=false;
    s.page=PC_FISHING;pc_fishing_cancel(&s.play);save(d,argv[1],"fishing-idle",&s,false,82);
    pc_fishing_key(&s.play,2,11000);save(d,argv[1],"fishing-wait",&s,false,82);
    pc_play_tick(&s.play,11000+s.play.wait_ms);save(d,argv[1],"fishing-bite",&s,false,82);
    pc_fishing_key(&s.play,2,s.play.now+500);save(d,argv[1],"fishing-caught",&s,false,82);
    pc_fishing_key(&s.play,2,20000);pc_fishing_key(&s.play,2,20001);save(d,argv[1],"fishing-early",&s,false,82);
    pc_fishing_key(&s.play,2,21000);pc_play_tick(&s.play,30000);save(d,argv[1],"fishing-missed",&s,false,82);
    s.play.fishing=PC_FISH_BOOK;s.play.book=5;s.play.progress.fish[5]=0;
    save(d,argv[1],"fish-unknown",&s,false,82);
    for(unsigned i=0;i<PC_FISH_COUNT;i++) {
        s.play.book=(uint8_t)i;s.play.progress.fish[i]=999;
        char name[32];snprintf(name,sizeof(name),"fish-book-%u",i);save(d,argv[1],name,&s,false,82);
    }
    printf("Screen preview: PASS (%u real LVGL states, visible glyphs, label bounds and rendered art)\n",captures);
    assert(frames);lv_deinit();return 0;
}
