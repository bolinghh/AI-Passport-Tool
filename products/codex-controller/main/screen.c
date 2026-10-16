#include "bsp_display.h"
#include "lvgl.h"
#include "runtime.h"
#include "screen_play.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

LV_FONT_DECLARE(passport_cjk_16);
LV_FONT_DECLARE(passport_ui_20);

/* Physical 240 x 320 coordinates, not a scaled concept canvas. Pixel art is
 * drawn directly into LVGL's existing partial buffer; no full-screen bitmap. */
static const uint32_t BG=0x061b34, FG=0xffedbd, MUTED=0x9dbbd5, DIM=0x3c5a76;
static const uint32_t AMBER=0xffbd55, GREEN=0x6fddb1, BLUE=0x55c8ed;
static lv_obj_t *screen, *battery_label, *title, *date_label, *detail, *footer;
static lv_obj_t *rows[4], *hint, *dialog_title, *dialog_detail, *dialog_hint;
static pc_controller_t view;
static bool show_pair, audio_available;
static unsigned audio_level;
static int battery_percent, signal_rssi;
static uint32_t pairing_code;

static const uint8_t digits[10][10] = {
    {28,62,99,99,99,99,99,99,62,28},
    {12,28,60,12,12,12,12,12,63,63},
    {62,127,99,3,6,12,24,48,127,127},
    {62,127,99,3,30,30,3,99,127,62},
    {6,14,30,54,102,102,127,127,6,6},
    {127,127,96,96,126,127,3,99,127,62},
    {30,62,96,96,126,127,99,99,127,62},
    {127,127,3,6,12,24,24,24,24,24},
    {62,127,99,99,62,62,99,99,127,62},
    {62,127,99,99,127,63,3,3,62,60}
};
static void pixel(lv_layer_t *layer,int x,int y,int w,int h,uint32_t color) {
    if(w<=0 || h<=0) return;
    lv_area_t area={x,y,x+w-1,y+h-1}, clipped;
    clipped=(lv_area_t){LV_MAX(area.x1,layer->_clip_area.x1),LV_MAX(area.y1,layer->_clip_area.y1),
                        LV_MIN(area.x2,layer->_clip_area.x2),LV_MIN(area.y2,layer->_clip_area.y2)};
    if(clipped.x1>clipped.x2 || clipped.y1>clipped.y2) return;
    lv_draw_fill_dsc_t d;lv_draw_fill_dsc_init(&d);
    d.color=lv_color_hex(color);d.opa=LV_OPA_COVER;
    lv_draw_fill(layer,&d,&clipped);
}
static void art_pixel(void *context,int x,int y,int w,int h,uint32_t color) {
    pixel(context,x,y,w,h,color);
}
static pc_canvas_t canvas(lv_layer_t *layer) {return (pc_canvas_t){layer,art_pixel};}
static void app_icon(lv_layer_t *,int,int,unsigned);
static void frame(lv_layer_t *layer,int x,int y,int w,int h,uint32_t color) {
    /* Stepped corners keep the selection aligned to the sprite grid. */
    pixel(layer,x+6,y,w-12,2,color); pixel(layer,x+6,y+h-2,w-12,2,color);
    pixel(layer,x,y+6,2,h-12,color); pixel(layer,x+w-2,y+6,2,h-12,color);
    for(int corner=0;corner<2;corner++) {
        int cy=corner?y+h-6:y+2;
        pixel(layer,x+2,cy,2,4,color); pixel(layer,x+w-4,cy,2,4,color);
    }
    pixel(layer,x+4,y+2,w-8,2,color); pixel(layer,x+4,y+h-4,w-8,2,color);
}
static void number(lv_layer_t *layer,const char *text,int y,unsigned scale) {
    int width=0;
    for(const char *p=text;*p;p++) width+=(*p==':'?3:7)*(int)scale+(int)scale;
    width-=(int)scale;
    int x=(240-width)/2;
    for(;*text;text++) {
        if(*text==':') {
            pixel(layer,x,y+3*scale,2*scale,2*scale,FG);
            pixel(layer,x,y+7*scale,2*scale,2*scale,FG);
        } else if(*text>='0' && *text<='9') {
            for(int r=0;r<10;r++) for(int c=0;c<7;c++)
                if(digits[*text-'0'][r]&(1<<(6-c)))
                    pixel(layer,x+c*scale,y+r*scale,scale,scale,FG);
        } else pixel(layer,x+scale,y+4*scale,5*scale,2*scale,DIM);
        x+=(*text==':'?3:7)*(int)scale+(int)scale;
    }
}
/* Palette indices: outline, deep/body/light, green stem, metal and shine.
 * Each authored sprite occupies 18 x 20 pixels at 2x physical scale. */
static const char *const sprites[3][20]={
    {"..................","........gg........",".....gggGGggg.....",".......gGGg.......",
     "....oooGGGooo.....","...oRRRGGGRRRo....","..oRRRRRRRRRRRo...",".oRRHHRRRRRRRRRo..",
     ".oRHHHRRRRRRRRRo..",".oRHHHRRRRRRRRRo..",".oRRHRRRRRRRRRRo..",".oRRRRRRRRRRRRRo..",
     ".oRRRRRRRRRRRrRo..","..oRRRRRRRRRrrro..","..oRRRRRRRrrrro...","...oRRRRrrrrro....",
     "....orrrrrrro.....",".....ooooooo......","..................",".................."},
    {".......oooo.......",".....ooGHHGoo.....",".....oGHHGGGo.....",".....oGHHGGGo.....",
     ".....oGHGGGGo.....",".....oGGGGGGo.....",".....oGGGGGGo.....",".....oGGGGGGo.....",
     ".....ogGGGGGo.....","...M.oggggggo.M...","...M..oggggo..M...","...M...oooo...M...",
     "...MM........MM...","....MM......MM....",".....MMMMMMMM.....",".......MM.........",
     ".......MM.........",".....MMMMMM.......",".....MMMMMM.......",".................."},
    {".......MMMM.......",".......MHHM.......","..MMM..MHHM..MMM..","..MHHMMHHHHMMHHM..",
     "..MHHHHHHHHHHHHM..","...MHHHHHHHHHHM...","...MHHHooooHHHM...","MMMHHHoo..ooHHHMMM",
     "MHHHHHo....oHHHHHM","MHHHHHo....oHHHHHM","MHHHHHo....oHHHHHM","MMMHHHoo..ooHHHMMM",
     "...MHHHooooHHHM...","...MHHHHHHHHHHM...","..MHHHHHHHHHHHHM..","..MHHMMHHHHMMHHM..",
     "..MMM..MHHM..MMM..",".......MHHM.......",".......MMMM.......",".................."}
};
static uint32_t ink(char c,unsigned kind) {
    switch(c) {
    case 'o': return 0x001125;
    case 'R': return 0xff6346;
    case 'r': return 0xd94037;
    case 'g': return 0x248d77;
    case 'G': return 0x5bd3a2;
    case 'M': return 0x7e9eb8;
    case 'H': return kind==0?0xffb38b:kind==1?0x9af2c6:0xb5d3e5;
    default: return BG;
    }
}
static void icon(lv_layer_t *layer,int x,int y,unsigned kind) {
    for(int r=0;r<20;r++) for(int c=0;c<18;c++)
        if(sprites[kind][r][c]!='.') pixel(layer,x+c*2,y+r*2,2,2,ink(sprites[kind][r][c],kind));
}
static void app_icon(lv_layer_t *layer,int x,int y,unsigned app) {
    if(app==PC_APP_PET) pc_art_pet(canvas(layer),x+2,y+2,2,view.play.progress.outfit,PC_PET_QUIET,2);
    else if(app==PC_APP_FISHING) pc_art_fish(canvas(layer),x,y+12,2,2,true);
    else icon(layer,x,y,app==PC_APP_SETTINGS?2:app);
}
static void cloud(lv_layer_t *layer,int x,int y,bool pale) {
    uint32_t top=pale?FG:0xb3d3e7;
    pixel(layer,x+10,y,12,6,top);pixel(layer,x+6,y+6,22,6,top);
    pixel(layer,x,y+12,36,6,top);pixel(layer,x+2,y+18,40,4,0x729ab8);
    pixel(layer,x+22,y+10,12,10,0x8db8d2);
}
static void sun(lv_layer_t *layer,int x,int y) {
    pixel(layer,x+14,y,12,4,AMBER);pixel(layer,x+8,y+4,24,4,AMBER);
    pixel(layer,x+4,y+8,32,24,AMBER);pixel(layer,x+8,y+32,24,4,AMBER);
    pixel(layer,x+14,y+36,12,4,AMBER);
    pixel(layer,x+10,y+8,14,8,0xffdc76);pixel(layer,x+8,y+16,8,10,0xffd66a);
    pixel(layer,x+17,y-10,6,6,AMBER);pixel(layer,x+17,y+44,6,6,AMBER);
    pixel(layer,x-10,y+17,6,6,AMBER);pixel(layer,x+44,y+17,6,6,AMBER);
    for(int i=0;i<2;i++) for(int j=0;j<2;j++)
        pixel(layer,x+(i?38:-4),y+(j?38:-4),6,6,AMBER);
}
static void weather(lv_layer_t *layer) {
    uint16_t code=view.weather_code;
    if(code<=2) {
        sun(layer,100,181);
        if(code) cloud(layer,121,203,false);
        else {cloud(layer,52,211,true);cloud(layer,151,211,true);}
    } else {
        cloud(layer,86,184,false);cloud(layer,111,191,false);
        if(code>=95) {
            pixel(layer,116,214,10,6,AMBER);pixel(layer,112,220,10,6,AMBER);
            pixel(layer,118,226,6,6,AMBER);
        } else if(code>=71 && code<=77) {
            for(int i=0;i<3;i++) {
                pixel(layer,94+i*18,219,6,2,FG);pixel(layer,96+i*18,217,2,6,FG);
            }
        } else if(code>=51) {
            for(int i=0;i<3;i++) pixel(layer,95+i*16,218+(i%2)*4,4,8,BLUE);
        } else if(code>=45) {
            pixel(layer,85,218,66,2,MUTED);pixel(layer,94,224,48,2,MUTED);
        }
    }
}
static void status(lv_layer_t *layer) {
    uint32_t bt=view.ready?BLUE:DIM;
    /* Bluetooth rune, then four bars derived from real RSSI. */
    pixel(layer,21,12,2,18,bt);
    for(int i=0;i<4;i++) {
        pixel(layer,23+i*2,14+i*2,2,2,bt);pixel(layer,23+i*2,26-i*2,2,2,bt);
        pixel(layer,15+i*2,17+i*2,2,2,bt);pixel(layer,15+i*2,25-i*2,2,2,bt);
    }
    int bars=!view.ready || signal_rssi==127?0:signal_rssi>=-55?4:signal_rssi>=-67?3:signal_rssi>=-80?2:1;
    for(int i=0;i<4;i++) pixel(layer,38+i*6,25-i*4,4,5+i*4,i<bars?0x86e4ee:DIM);
    if(!view.ready || signal_rssi==127) pixel(layer,68,23,6,2,MUTED);
    pixel(layer,201,14,24,2,MUTED);pixel(layer,201,26,24,2,MUTED);
    pixel(layer,201,16,2,10,MUTED);pixel(layer,223,16,2,10,MUTED);
    pixel(layer,225,18,3,6,MUTED);
    if(battery_percent>=0) pixel(layer,205,18,17*battery_percent/100,6,battery_percent<=15?AMBER:GREEN);
    else pixel(layer,211,20,6,2,DIM);
}
static bool modal(void) {return show_pair || view.confirm || view.timer.alert;}
static void draw(lv_event_t *event) {
    lv_layer_t *layer=lv_event_get_layer(event);
    status(layer);
    pixel(layer,14,282,212,1,DIM);
    char text[16];
    if(modal()) {
        pixel(layer,20,78,200,184,0x112c45);
        frame(layer,14,72,212,196,AMBER);
        if(show_pair) {
            snprintf(text,sizeof(text),"%06lu",(unsigned long)pairing_code);
            number(layer,text,141,4);
        }
        return;
    }
    if(view.page==PC_HOME) {
        if(view.clock_valid) {
            time_t local=(time_t)((int64_t)view.epoch+view.utc_offset);struct tm t;
            gmtime_r(&local,&t);snprintf(text,sizeof(text),"%02d:%02d",t.tm_hour,t.tm_min);
        } else strcpy(text,"--:--");
        number(layer,text,65,6);
        if(view.weather_valid) weather(layer);
    } else if(view.page==PC_APPS) {
        unsigned first=pc_app_window(view.app);
        for(unsigned slot=0;slot<3;slot++) {
            unsigned app=first+slot;int y=77+(int)slot*66;
            if(view.app==app) {
                pixel(layer,16,y+4,208,54,0x35352f);
                frame(layer,12,y,216,62,AMBER);
                for(int n=0;n<5;n++) pixel(layer,20+n*2,y+21+n*2,2,18-n*4,AMBER);
            }
            app_icon(layer,39,y+11,app);
        }
        pixel(layer,232,85,2,178,DIM);pixel(layer,232,85+(int)first*59,2,60,AMBER);
    } else if(view.page==PC_POMODORO) {
        uint32_t seconds=pc_timer_seconds(&view.timer);
        snprintf(text,sizeof(text),"%02lu:%02lu",(unsigned long)(seconds/60),(unsigned long)(seconds%60));
        number(layer,text,86,strlen(text)>5?5:6);
        for(unsigned i=0;i<2;i++) if(view.operation==i) {
            pixel(layer,22,174+(int)i*44,196,36,0x35352f);
            frame(layer,20,172+(int)i*44,200,40,AMBER);
        }
    } else if(view.page==PC_MICROPHONE) {
        icon(layer,102,85,1);
        for(unsigned i=0;i<22;i++) {
            unsigned height=4+(i%5)*3;
            bool lit=view.mic==PC_LIVE && i*100/22<audio_level;
            pixel(layer,33+(int)i*8,186-(int)height/2,4,(int)height,lit?GREEN:DIM);
        }
    } else if(view.page==PC_PET) {
        pc_art_pet_room(canvas(layer),&view.play);
        int y=210+view.play.pet_action*24;
        pixel(layer,22,y+2,196,20,0x35352f);frame(layer,20,y,200,24,AMBER);
    } else if(view.page==PC_FISHING) {
        pc_art_pond(canvas(layer),&view.play);
    } else if(view.page==PC_SETTINGS) {
        int y=79+view.setting*46;
        pixel(layer,18,y,204,40,0x35352f);frame(layer,16,y-2,208,44,AMBER);
    }
}
static lv_obj_t *label(int x,int y,int w,int h,uint32_t color,const lv_font_t *font) {
    lv_obj_t *o=lv_label_create(screen);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_text_font(o,font,0);lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);
    lv_label_set_text(o,"");return o;
}
esp_err_t pc_screen_init(void) {
    if(!bsp_lvgl_lock(1000)) return ESP_ERR_TIMEOUT;
    screen=lv_obj_create(NULL);lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen,lv_color_hex(BG),0);lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
    lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(screen,draw,LV_EVENT_DRAW_MAIN_END,NULL);
    battery_label=label(142,8,54,26,FG,&passport_ui_20);
    title=label(20,42,200,28,FG,&passport_ui_20);
    date_label=label(16,141,208,24,FG,&passport_cjk_16);
    detail=label(16,237,208,44,FG,&passport_cjk_16);
    footer=label(14,290,212,22,MUTED,&passport_cjk_16);
    hint=label(12,257,216,22,AMBER,&passport_cjk_16);
    for(int i=0;i<4;i++) rows[i]=label(26,87+i*46,188,28,FG,&passport_cjk_16);
    dialog_title=label(24,91,192,28,FG,&passport_ui_20);
    dialog_detail=label(26,139,188,66,FG,&passport_cjk_16);
    dialog_hint=label(26,229,188,24,AMBER,&passport_cjk_16);
    lv_screen_load(screen);bsp_lvgl_unlock();return ESP_OK;
}
static void set(lv_obj_t *o,const char *text) {
    if(strcmp(lv_label_get_text(o),text)) lv_label_set_text(o,text);
    if(*text) lv_obj_remove_flag(o,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(o,LV_OBJ_FLAG_HIDDEN);
}
static void row(unsigned i,const char *text,bool selected) {
    set(rows[i],text);lv_obj_set_style_text_color(rows[i],lv_color_hex(selected?AMBER:FG),0);
}
void pc_screen_render(const pc_controller_t *s,uint32_t passkey,bool pairing,int battery,
                      unsigned level,bool audio_ok,unsigned setting) {
    (void)setting;
    if(!bsp_lvgl_lock(100)) return;
    if(!screen) {bsp_lvgl_unlock();return;}
    view=*s;show_pair=pairing;pairing_code=passkey;audio_available=audio_ok;audio_level=level>100?100:level;
    battery_percent=battery>100?100:battery;signal_rssi=pc_link_rssi();
    char text[128];
    if(battery<0) strcpy(text,"--");else snprintf(text,sizeof(text),"%d%%",battery_percent);
    set(battery_label,text);
    set(title,"");set(date_label,"");set(detail,"");set(hint,"");
    set(footer,"长按确定返回");
    set(dialog_title,"");set(dialog_detail,"");set(dialog_hint,"");
    lv_obj_set_style_text_color(hint,lv_color_hex(AMBER),0);
    lv_obj_set_style_text_font(date_label,&passport_cjk_16,0);
    lv_obj_set_style_text_font(detail,&passport_cjk_16,0);
    lv_obj_set_pos(date_label,16,141);lv_obj_set_size(detail,208,44);lv_obj_set_pos(detail,16,237);
    for(unsigned i=0;i<4;i++) {
        row(i,"",false);lv_obj_set_height(rows[i],28);lv_obj_set_pos(rows[i],26,87+(int)i*46);lv_obj_set_width(rows[i],188);
        lv_obj_set_style_text_align(rows[i],LV_TEXT_ALIGN_CENTER,0);
        lv_obj_set_style_text_font(rows[i],&passport_cjk_16,0);
    }
    switch(s->page) {
    case PC_HOME: {
        if(s->clock_valid) {
            time_t local=(time_t)((int64_t)s->epoch+s->utc_offset);struct tm t;gmtime_r(&local,&t);
            const char *days[]={"周日","周一","周二","周三","周四","周五","周六"};
            snprintf(text,sizeof(text),"%d月%d日  %s",t.tm_mon+1,t.tm_mday,days[t.tm_wday]);
        } else strcpy(text,"等待电脑同步时间");
        if(s->clock_valid) lv_obj_set_style_text_font(date_label,&passport_ui_20,0);
        set(date_label,text);
        if(s->weather_valid) {
            const char *condition=s->weather_code==0?"晴":s->weather_code<=3?"多云":
                s->weather_code<=48?"雾":s->weather_code>=95?"雷雨":s->weather_code>=71&&s->weather_code<=77?"雪":"雨";
            snprintf(text,sizeof(text),"%s  %s%d.%d C",condition,s->temperature<0?"-":"",
                     s->temperature<0?-(s->temperature/10):s->temperature/10,
                     s->temperature<0?-(s->temperature%10):s->temperature%10);
            set(detail,text);lv_obj_set_height(detail,24);
            lv_obj_set_style_text_font(detail,&passport_ui_20,0);
            /* Long cities retain a whole line; stale data gets an explicit mark. */
            snprintf(text,sizeof(text),"%s%s",pc_weather_stale(s)?"已过期 · ":"",s->city);
            set(hint,text);lv_obj_set_style_text_color(hint,lv_color_hex(pc_weather_stale(s)?AMBER:MUTED),0);
        } else {lv_obj_set_pos(detail,16,209);set(detail,"请在 Mac 设置天气城市");}
        if(s->timer.state==PC_TIMER_RUNNING || s->timer.state==PC_TIMER_PAUSED) {
            uint32_t sec=pc_timer_seconds(&s->timer);
            snprintf(text,sizeof(text),"%s %02lu:%02lu%s",s->timer.rest?"休息":"专注",(unsigned long)(sec/60),
                     (unsigned long)(sec%60),s->timer.state==PC_TIMER_PAUSED?" 暂停":"");
            set(footer,text);
        } else set(footer,"按确定键打开应用");
        break;
    }
    case PC_APPS: {
        set(title,"应用");const char *names[]={"番茄钟","麦克风","像素伙伴","迷你钓鱼","设置"};
        unsigned first=pc_app_window(s->app);
        for(unsigned i=0;i<3;i++) {
            lv_obj_set_pos(rows[i],91,96+(int)i*66);lv_obj_set_width(rows[i],120);
            lv_obj_set_style_text_align(rows[i],LV_TEXT_ALIGN_LEFT,0);
            lv_obj_set_style_text_font(rows[i],&passport_ui_20,0);row(i,names[first+i],s->app==first+i);
        }
        set(footer,"上下选择 · 确定打开");break;
    }
    case PC_POMODORO:
        set(title,s->timer.rest?"休息时间":"专注时间");
        lv_obj_set_pos(rows[0],26,180);lv_obj_set_pos(rows[1],26,224);
        row(0,s->timer.state==PC_TIMER_RUNNING?"暂停":s->timer.state==PC_TIMER_PAUSED?"继续":
            s->timer.state==PC_TIMER_DONE?(s->timer.rest?"开始专注":"开始休息"):"开始",s->operation==0);
        row(1,"重置",s->operation==1);break;
    case PC_MICROPHONE:
        set(title,"麦克风");
        set(date_label,!audio_available?"采集初始化失败":s->mic==PC_LIVE?"正在传音":s->mic==PC_PREPARING?"准备中":"麦克风已关闭");
        set(detail,s->mic==PC_OFF?"确定开始传音":"确定停止传音");break;
    case PC_PET: {
        const pc_play_t *p=&s->play;set(title,"像素伙伴");
        lv_obj_set_pos(date_label,16,65);
        snprintf(text,sizeof(text),"小团  Lv.%u · %lu/100",pc_pet_level(&p->progress),(unsigned long)(p->progress.xp%100));
        if(pc_pet_level(&p->progress)==10) snprintf(text,sizeof(text),"小团  Lv.10 · 满级");
        set(date_label,text);lv_obj_set_pos(detail,16,184);lv_obj_set_height(detail,22);
        const char *responses[]={"","好吃！谢谢你","最喜欢陪你玩","零食不足，去钓鱼吧",""};
        if(p->reaction==PC_PET_DRESSED && pc_pet_outfits(&p->progress)==1) strcpy(text,"Lv.2 解锁围巾");
        else if(p->reaction==PC_PET_DRESSED && pc_pet_outfits(&p->progress)==2) strcpy(text,"Lv.4 解锁星冠");
        else if(*responses[p->reaction]) snprintf(text,sizeof(text),"%s",responses[p->reaction]);
        else snprintf(text,sizeof(text),"零食 %u · 鱼种 %u/%u",p->progress.treats,pc_fish_discovered(&p->progress),PC_FISH_COUNT);
        set(detail,text);
        const char *outfits[]={"装扮：原样","装扮：围巾","装扮：星冠"};
        const char *actions[]={"喂食","陪玩",outfits[p->progress.outfit]};
        for(unsigned i=0;i<3;i++) {
            lv_obj_set_pos(rows[i],26,213+(int)i*24);lv_obj_set_height(rows[i],21);row(i,actions[i],p->pet_action==i);
        }
        break;
    }
    case PC_FISHING: {
        const pc_play_t *p=&s->play;set(title,p->fishing==PC_FISH_BOOK?"鱼类图鉴":"迷你钓鱼");
        lv_obj_set_pos(date_label,16,218);lv_obj_set_pos(detail,16,241);lv_obj_set_height(detail,23);
        if(p->fishing==PC_FISH_BOOK) {
            bool known=p->progress.fish[p->book]>0;
            set(date_label,known?pc_fish_name(p->book):"未知鱼种");
            if(known) snprintf(text,sizeof(text),"%s · 已钓到 %u 条",pc_fish_rarity(p->book),p->progress.fish[p->book]);
            else snprintf(text,sizeof(text),"尚未发现 · %u/%u",(unsigned)p->book+1,PC_FISH_COUNT);
            set(detail,text);set(footer,"上下翻页 · 确定返回");
        } else if(p->fishing==PC_FISH_CAUGHT) {
            snprintf(text,sizeof(text),"钓到了：%s",pc_fish_name(p->species));set(date_label,text);
            set(detail,"+8 经验 · +1 零食");set(hint,pc_fish_rarity(p->species));
            set(footer,"确定再钓 · 上下图鉴");
        } else if(p->fishing==PC_FISH_BITE) {
            set(date_label,"咬钩啦！快按确定");set(detail,"及时收竿就能钓到鱼");set(footer,"确定收竿");
        } else if(p->fishing==PC_FISH_WAIT) {
            set(date_label,"静静等鱼上钩…");set(detail,"浮漂下沉时按确定");set(footer,"长按确定收竿返回");
        } else {
            set(date_label,p->fishing==PC_FISH_MISSED?(p->early?"收竿太早啦":"鱼溜走了，再试一次"):"林间小池");
            snprintf(text,sizeof(text),"图鉴 %u/%u · 零食 %u",pc_fish_discovered(&p->progress),PC_FISH_COUNT,p->progress.treats);
            set(detail,text);set(footer,"确定抛竿 · 上下图鉴");
        }
        break;
    }
    case PC_SETTINGS: {
        set(title,"设置");const char *brightness[]={"亮度：低","亮度：中","亮度：高"};
        row(0,brightness[s->brightness],s->setting==0);
        if(s->ready && signal_rssi!=127) snprintf(text,sizeof(text),"蓝牙已连接  %d dBm",signal_rssi);
        else snprintf(text,sizeof(text),"%s",s->ready?"蓝牙已连接  --":"蓝牙未连接");
        row(1,text,s->setting==1);row(2,"取消绑定",s->setting==2);row(3,"返回应用列表",s->setting==3);break;
    }
    }
    if(s->page==PC_PET || s->page==PC_FISHING) {
        if(s->play.save_failed) set(footer,"存档不可用");
        else if(s->play.saving && s->play.fishing!=PC_FISH_WAIT && s->play.fishing!=PC_FISH_BITE) set(footer,"正在保存进度");
    }
    if(s->hint==PC_HINT_UPGRADE) set(hint,"请升级固件或 Mac 应用");
    else if(s->page==PC_MICROPHONE) {
        const char *messages[]={"","请连接 Mac","请检查音频输出","连接超时","采集失败","版本不兼容"};
        set(hint,messages[s->hint]);
    }
    if(modal()) {
        set(title,"");set(date_label,"");set(detail,"");set(hint,"");
        for(unsigned i=0;i<4;i++) set(rows[i],"");
        if(pairing) {
            set(dialog_title,"蓝牙安全配对");
            set(dialog_hint,"请按 Mac 系统提示配对");set(footer,"配对后自动连接");
        } else if(s->timer.alert) {
            set(dialog_title,"计时完成");set(dialog_detail,s->timer.rest?"休息结束\n准备好再开始专注":"专注完成\n准备好再开始休息");
            set(dialog_hint,"确定关闭提醒");
        } else {
            set(dialog_title,s->page==PC_SETTINGS?"取消绑定":"重置计时");
            set(dialog_detail,s->page==PC_SETTINGS?"确定删除绑定？":"确定重置？");
            set(dialog_hint,"确定确认 · 上下取消");
        }
    }
    lv_obj_invalidate(screen);bsp_lvgl_unlock();
}
