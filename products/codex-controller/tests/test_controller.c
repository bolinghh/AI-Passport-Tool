#include "controller.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned received;
static uint8_t encoded[PC_FRAME_MAX];
static size_t encoded_length;
static void message(const pc_message_t *m, void *ctx) {
    (void)ctx;
    ++received;
    uint8_t actual[PC_FRAME_MAX];
    assert(pc_encode(actual, sizeof(actual), m) == encoded_length);
    assert(!memcmp(actual, encoded, encoded_length));
}
static void vectors(const char *file) {
    FILE *f = fopen(file, "r");
    assert(f);
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n')
            continue;
        encoded_length = 0;
        char *at = line;
        unsigned n;
        int used;
        while (sscanf(at, "%x%n", &n, &used) == 1) {
            encoded[encoded_length++] = (uint8_t)n;
            at += used;
        }
        for (size_t split = 0; split <= encoded_length; split++) {
            pc_parser_t parser = {0};
            received = 0;
            pc_feed(&parser, encoded, split, message, NULL);
            pc_feed(&parser, encoded + split, encoded_length - split, message, NULL);
            assert(received == 1);
        }
    }
    fclose(f);
}
static void malformed(void) {
    pc_parser_t parser = {0};
    uint8_t zeros[PC_FRAME_MAX * 4] = {0};
    pc_message_t hello = {.type = PC_HELLO};
    encoded_length = pc_encode(encoded, sizeof(encoded), &hello);
    received = 0;
    pc_feed(&parser, zeros, sizeof(zeros), message, NULL);
    assert(parser.used <= PC_FRAME_MAX);
    uint8_t bad[PC_HEADER] = {0xa5, 0x5a, 1, PC_AUDIO, 0xff, 0xff};
    pc_feed(&parser, bad, sizeof(bad), message, NULL);
    pc_feed(&parser, encoded, encoded_length, message, NULL);
    pc_feed(&parser, encoded, encoded_length, message, NULL);
    assert(received == 2);
    assert(!pc_utf8((const uint8_t *)"\xe4\xb8", 2));
    uint8_t payload[PC_AUDIO_MAX + 1] = {0}, tx[PC_TX_MAX];
    pc_message_t audio = {.type = PC_AUDIO, .length = PC_AUDIO_MAX, .payload = payload};
    assert(pc_encode(tx, sizeof(tx), &audio) == PC_TX_MAX);
    audio.length++;
    assert(pc_encode(tx, sizeof(tx), &audio) == 0);
}
static void logic(void) {
    pc_controller_t s; pc_controller_init(&s);
    assert(s.page==PC_HOME && s.timer.focus_minutes==25);
    pc_controller_key(&s,2,false,0); assert(s.page==PC_APPS);
    pc_controller_key(&s,0,false,0); assert(s.app==PC_APP_SETTINGS);
    pc_controller_key(&s,1,false,0); assert(s.app==0);
    pc_controller_key(&s,2,false,0); assert(s.page==PC_POMODORO);
    pc_controller_key(&s,2,false,100); assert(s.timer.state==PC_TIMER_RUNNING);
    pc_controller_tick(&s,60100); assert(pc_timer_seconds(&s.timer)==1440);
    pc_controller_key(&s,2,false,60100); assert(s.timer.state==PC_TIMER_PAUSED);
    pc_controller_tick(&s,90000); assert(pc_timer_seconds(&s.timer)==1440);
    pc_controller_key(&s,2,false,90000);
    pc_controller_key(&s,2,true,90000); assert(s.page==PC_APPS);
    pc_controller_key(&s,2,false,90001); assert(s.page==PC_APPS); // Held release is consumed.
    pc_controller_disconnect(&s); pc_controller_tick(&s,1530000);
    assert(s.timer.state==PC_TIMER_DONE && s.timer.alert && s.timer.completion==1);
    pc_controller_tick(&s,1531000); assert(s.timer.completion==1);
    pc_controller_key(&s,2,false,1531000); assert(!s.timer.alert);
    s.page=PC_POMODORO; s.operation=0;
    pc_controller_key(&s,2,false,1531000);
    assert(s.timer.rest && s.timer.remaining_ms==300000 && s.timer.state==PC_TIMER_RUNNING);
    s.ready=true; s.last_rx=1531000;
    uint8_t settings[]={30,10}; pc_message_t update={.type=PC_SETTINGS_UPDATE,.length=2,.payload=settings};
    assert(pc_controller_update(&s,&update,1531000));
    assert(s.timer.remaining_ms==300000); // Running duration is immutable.
    settings[0]=0; assert(!pc_controller_update(&s,&update,0));
    pc_controller_key(&s,1,false,1531000);pc_controller_key(&s,2,false,1531000);
    assert(s.confirm);pc_controller_key(&s,0,false,1531000);assert(!s.confirm && s.timer.state==PC_TIMER_RUNNING);
    pc_controller_key(&s,2,false,1531000);pc_controller_key(&s,2,false,1531000);
    assert(s.timer.state==PC_TIMER_IDLE && s.timer.remaining_ms==600000);
    s.page=PC_MICROPHONE;
    pc_action_t a=pc_controller_key(&s,2,false,1531000);
    assert(a.type==PC_MIC && a.value==1 && s.mic==PC_PREPARING);
    pc_action_t cancel=pc_controller_key(&s,2,true,1531001);
    assert(!cancel.value && s.mic==PC_OFF && s.page==PC_APPS);
    uint8_t ack[]={0,1};pc_message_t m={.type=PC_MIC_ACK,.request=a.request,.stream=a.stream,.length=2,.payload=ack};
    pc_controller_ack(&s,&m);assert(s.mic==PC_OFF); // Stale start acknowledgement.
    ack[1]=0;m.request=cancel.request;m.stream=cancel.stream;pc_controller_ack(&s,&m);assert(!s.pending);
    s.page=PC_MICROPHONE;s.suppress_click=false;a=pc_controller_key(&s,2,false,1531100);
    ack[1]=1;m.request=a.request;m.stream=a.stream;pc_controller_ack(&s,&m);assert(s.mic==PC_LIVE);
    s.timer.alert=true;pc_action_t interrupted=pc_controller_key(&s,2,false,1531200);
    assert(interrupted.type==PC_MIC && !interrupted.value && s.mic==PC_OFF && !s.timer.alert);
    pc_controller_disconnect(&s);assert(!s.ready && !s.stream && s.mic==PC_OFF);
    s.ready=true;s.last_rx=UINT32_MAX-100;
    assert(!pc_controller_tick(&s,50));assert(pc_controller_tick(&s,7100));
    s.ready=true;uint8_t clock[8];pc_put32(clock,1800000000);pc_put32(clock+4,28800);
    m=(pc_message_t){.type=PC_TIME,.length=8,.payload=clock};assert(pc_controller_update(&s,&m,UINT32_MAX-100));
    s.last_rx=UINT32_MAX-100;pc_controller_tick(&s,1900);assert(s.epoch==1800000002);
    uint8_t weather[12];pc_put32(weather,s.epoch);weather[4]=0;weather[5]=230;weather[6]=0;weather[7]=0;
    weather[8]=3;memcpy(weather+9,"ABC",3);m=(pc_message_t){.type=PC_WEATHER,.length=12,.payload=weather};
    assert(pc_controller_update(&s,&m,1900));assert(!pc_weather_stale(&s));
    weather[8]=49;assert(!pc_controller_update(&s,&m,1900));assert(!strcmp(s.city,"ABC"));
    s.epoch+=3600;assert(pc_weather_stale(&s));
    assert(!pc_utf8((const uint8_t *)"\xc0\xaf",2));
    assert(!pc_utf8((const uint8_t *)"\xed\xa0\x80",3));
}
int main(int argc, char **argv) {
    // Windows initializes CCCD to zero during discovery while pairing is pending.
    assert(!pc_subscription_lost(false, false));
    assert(!pc_subscription_lost(false, true));
    assert(!pc_subscription_lost(true, true));
    assert(pc_subscription_lost(true, false));
    uint8_t capability[]={2,15};pc_message_t cap={.type=PC_CAPABILITIES,.length=2,.payload=capability};
    assert(pc_capabilities(&cap));capability[0]=1;assert(!pc_capabilities(&cap));
    capability[0]=2;capability[1]=1;assert(!pc_capabilities(&cap));
    cap.length=1;assert(!pc_capabilities(&cap));
    assert(argc == 2);
    vectors(argv[1]);
    malformed();
    logic();
    bool suppress = false;
    assert(pc_key_guard(&suppress, 2, true, false) == PC_KEY_IGNORED);
    assert(pc_key_guard(&suppress, 2, false, true) == PC_KEY_HOLD);
    assert(pc_key_guard(&suppress, 2, false, true) == PC_KEY_IGNORED);
    assert(pc_key_guard(&suppress, 0, true, false) == PC_KEY_IGNORED);
    assert(pc_key_guard(&suppress, 0, false, false) == PC_KEY_CLICK);
    assert(pc_key_guard(&suppress, 1, true, false) == PC_KEY_IGNORED);
    assert(pc_key_guard(&suppress, 1, false, false) == PC_KEY_CLICK);
    assert(pc_key_guard(&suppress, 2, false, false) == PC_KEY_IGNORED);
    assert(pc_key_guard(&suppress, 2, false, false) == PC_KEY_IGNORED);
    assert(pc_key_guard(&suppress, 2, true, false) == PC_KEY_IGNORED);
    assert(pc_key_guard(&suppress, 2, false, false) == PC_KEY_CLICK);
    puts("Controller and wire tests: PASS");
    return 0;
}
