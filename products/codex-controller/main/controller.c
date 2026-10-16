#include "controller.h"
#include <string.h>
bool pc_subscription_lost(bool previous, bool current) { return previous && !current; }
bool pc_capabilities(const pc_message_t *m) { return m->type==PC_CAPABILITIES && m->length==2 && m->payload && m->payload[0]==2 && m->payload[1]==15; }
pc_key_intent_t pc_key_guard(bool *suppress, unsigned key, bool pressed, bool held) {
    if (pressed) { if (key == 2) *suppress = false; return PC_KEY_IGNORED; }
    if (held) {
        if (key != 2 || *suppress) return PC_KEY_IGNORED;
        *suppress = true; return PC_KEY_HOLD;
    }
    return key == 2 && *suppress ? PC_KEY_IGNORED : PC_KEY_CLICK;
}
void pc_controller_init(pc_controller_t *s) {
    memset(s, 0, sizeof(*s)); s->next_request = 1; s->brightness = 1;
    s->timer.focus_minutes = 25; s->timer.rest_minutes = 5;
    s->timer.remaining_ms = 25 * 60000;
    pc_play_init(&s->play,1);
}
void pc_controller_stop(pc_controller_t *s) { s->mic = PC_OFF; s->stream = 0; s->pending = 0; }
void pc_controller_disconnect(pc_controller_t *s) {
    s->ready = false; pc_controller_stop(s); s->hint = PC_HINT_OFFLINE;
    /* Local navigation and timer deliberately survive a radio disconnect. */
}
static uint32_t request(pc_controller_t *s) {
    uint32_t n = s->next_request++; if (!n) n = s->next_request++; return n;
}
static pc_action_t stop(pc_controller_t *s, uint32_t now) {
    pc_action_t a = {0};
    if (s->mic != PC_OFF) {
        a = (pc_action_t){PC_MIC, 0, request(s), s->stream};
        pc_controller_stop(s); s->pending = a.request; s->request_at = now;
    }
    return a;
}
uint32_t pc_timer_seconds(const pc_timer_t *t) { return (t->remaining_ms + 999) / 1000; }
bool pc_weather_stale(const pc_controller_t *s) {
    return !s->weather_valid || !s->clock_valid || s->weather_at > s->epoch || s->epoch - s->weather_at >= 3600;
}
bool pc_controller_update(pc_controller_t *s, const pc_message_t *m, uint32_t now) {
    const uint8_t *p = m->payload;
    if (!s->ready) return false;
    if (m->type == PC_TIME && m->length == 8) {
        int32_t offset = (int32_t)pc_u32(p + 4);
        if (offset < -86400 || offset > 86400 || pc_u32(p) < 1577836800U) return false;
        s->epoch = pc_u32(p); s->utc_offset = offset; s->clock_at = now; s->clock_valid = true;
        return true;
    }
    if (m->type == PC_SETTINGS_UPDATE && m->length == 2) {
        if (!p[0] || p[0] > 120 || !p[1] || p[1] > 120) return false;
        s->timer.focus_minutes = p[0]; s->timer.rest_minutes = p[1];
        if (s->timer.state == PC_TIMER_IDLE)
            s->timer.remaining_ms = (s->timer.rest ? p[1] : p[0]) * 60000U;
        return true;
    }
    if (m->type == PC_WEATHER && m->length >= 9 && p[8] > 0 && p[8] <= 48 &&
        m->length == 9 + p[8] && pc_utf8(p + 9, p[8])) {
        int16_t temp = (int16_t)((p[4] << 8) | p[5]);
        if (temp < -1000 || temp > 700 || !pc_u32(p)) return false;
        s->weather_at = pc_u32(p); s->temperature = temp;
        s->weather_code = (uint16_t)((p[6] << 8) | p[7]);
        memcpy(s->city, p + 9, p[8]); s->city[p[8]] = 0; s->weather_valid = true;
        return true;
    }
    return false;
}
pc_page_t pc_app_page(unsigned app) {
    const pc_page_t pages[]={PC_POMODORO,PC_MICROPHONE,PC_PET,PC_FISHING,PC_SETTINGS};
    return app<PC_APP_COUNT?pages[app]:PC_HOME;
}
unsigned pc_app_window(unsigned app) {return app<2?0:app>=PC_APP_COUNT-1?PC_APP_COUNT-3:app-1;}
static void advance_timer(pc_controller_t *,uint32_t);
pc_action_t pc_controller_key(pc_controller_t *s, unsigned key, bool held, uint32_t now) {
    advance_timer(s,now);
    pc_action_t a = {0};
    if (held && key == 2) {
        pc_fishing_cancel(&s->play);
        s->suppress_click = true; s->confirm = false;
        a = stop(s, now);
        s->page = s->page == PC_APPS ? PC_HOME : s->page == PC_HOME ? PC_HOME : PC_APPS;
        return a;
    }
    if (held || key > 2) return a;
    if (key == 2 && s->suppress_click) { s->suppress_click = false; return a; }
    if (s->timer.alert && key < 2) return a;
    if (s->timer.alert && key == 2) { s->timer.alert = false; if(s->page!=PC_MICROPHONE || s->mic==PC_OFF) return a; }
    switch (s->page) {
    case PC_HOME:
        if (key == 2) s->page = PC_APPS;
        break;
    case PC_APPS:
        if (key < 2) s->app = (s->app + (key == 0 ? PC_APP_COUNT-1 : 1)) % PC_APP_COUNT;
        else { s->page = pc_app_page(s->app); s->operation = 0; s->confirm = false; }
        break;
    case PC_PET: pc_pet_key(&s->play,key,now); break;
    case PC_FISHING: pc_fishing_key(&s->play,key,now); break;
    case PC_MICROPHONE:
        if (key != 2) break;
        if (!s->ready) { s->hint = PC_HINT_OFFLINE; break; }
        if (s->mic != PC_OFF) return stop(s, now);
        if (s->pending) break;
        s->pending = request(s); s->stream = s->pending; s->request_at = now;
        s->mic = PC_PREPARING; s->hint = PC_HINT_NONE;
        return (pc_action_t){PC_MIC, 1, s->pending, s->stream};
    case PC_POMODORO:
        if (s->confirm) {
            if (key == 2) { s->timer.state = PC_TIMER_IDLE; s->timer.remaining_ms =
                (s->timer.rest ? s->timer.rest_minutes : s->timer.focus_minutes) * 60000U; }
            s->confirm = false; break;
        }
        if (key < 2) { s->operation ^= 1; break; }
        if (s->operation == 1) {
            if (s->timer.state == PC_TIMER_RUNNING || s->timer.state == PC_TIMER_PAUSED) s->confirm = true;
            else { s->timer.state = PC_TIMER_IDLE; s->timer.remaining_ms =
                (s->timer.rest ? s->timer.rest_minutes : s->timer.focus_minutes) * 60000U; }
        } else if (s->timer.state == PC_TIMER_RUNNING) s->timer.state = PC_TIMER_PAUSED;
        else {
            if (s->timer.state == PC_TIMER_DONE) {
                s->timer.rest = !s->timer.rest;
                s->timer.remaining_ms = (s->timer.rest ? s->timer.rest_minutes : s->timer.focus_minutes) * 60000U;
            }
            s->timer.state = PC_TIMER_RUNNING; s->timer.tick_at = now;
        }
        break;
    case PC_SETTINGS:
        if (s->confirm) { if (key != 2) s->confirm = false; break; } // Runtime executes confirmed unpair.
        if (key < 2) s->setting = (s->setting + (key == 0 ? 3 : 1)) % 4;
        else if (s->setting == 0) s->brightness = (s->brightness + 1) % 3;
        else if (s->setting == 2) s->confirm = true;
        else if (s->setting == 3) s->page = PC_APPS;
        break;
    }
    return a;
}
void pc_controller_ack(pc_controller_t *s, const pc_message_t *m) {
    if (!s->ready || m->type != PC_MIC_ACK || !s->pending || m->request != s->pending || m->length != 2) return;
    if (!m->payload[0] && m->payload[1] == 1 && s->mic == PC_PREPARING &&
        s->page == PC_MICROPHONE && m->stream == s->stream) s->mic = PC_LIVE;
    else {
        pc_controller_stop(s);
        if (m->payload[0]) s->hint = PC_HINT_ROUTE_FAILED;
    }
    s->pending = 0;
}
bool pc_controller_tick(pc_controller_t *s, uint32_t now) {
    if (s->clock_valid) {
        uint32_t secs = (now - s->clock_at) / 1000; s->epoch += secs; s->clock_at += secs * 1000;
    }
    advance_timer(s,now);
    pc_play_tick(&s->play,now);
    if ((s->ready && now - s->last_rx >= PC_TIMEOUT_MS) ||
        (s->pending && now - s->request_at >= PC_HEARTBEAT_MS)) {
        pc_controller_disconnect(s); s->hint = PC_HINT_TIMEOUT; return true;
    }
    return false;
}

static void advance_timer(pc_controller_t *s,uint32_t now) {
    if (s->timer.state == PC_TIMER_RUNNING) {
        uint32_t elapsed = now - s->timer.tick_at; s->timer.tick_at = now;
        if (elapsed >= s->timer.remaining_ms) {
            s->timer.remaining_ms = 0; s->timer.state = PC_TIMER_DONE; s->timer.alert = true;
            ++s->timer.completion;
            pc_fishing_cancel(&s->play);
            if(!s->timer.rest) pc_play_focus_completed(&s->play,now);
        } else s->timer.remaining_ms -= elapsed;
    }
}
