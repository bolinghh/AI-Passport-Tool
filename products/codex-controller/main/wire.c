#include "wire.h"
#include <string.h>

uint32_t pc_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
void pc_put32(uint8_t *p, uint32_t n) {
    p[0] = (uint8_t)(n >> 24);
    p[1] = (uint8_t)(n >> 16);
    p[2] = (uint8_t)(n >> 8);
    p[3] = (uint8_t)n;
}
size_t pc_encode(uint8_t *out, size_t cap, const pc_message_t *m) {
    if (m->length > PC_PAYLOAD_MAX || cap < PC_HEADER + m->length || (m->length && !m->payload))
        return 0;
    out[0] = 0xa5;
    out[1] = 0x5a;
    out[2] = PC_VERSION;
    out[3] = m->type;
    out[4] = (uint8_t)(m->length >> 8);
    out[5] = (uint8_t)m->length;
    pc_put32(out + 6, m->request);
    pc_put32(out + 10, m->stream);
    out[14] = (uint8_t)(m->sequence >> 8);
    out[15] = (uint8_t)m->sequence;
    if (m->length)
        memcpy(out + PC_HEADER, m->payload, m->length);
    return PC_HEADER + m->length;
}
static void consume(pc_parser_t *p, size_t n) {
    p->used -= n;
    memmove(p->data, p->data + n, p->used);
}
void pc_feed(pc_parser_t *p, const uint8_t *data, size_t len, pc_receive_fn fn, void *ctx) {
    /* A byte-wise bounded parser also works when ATT MTU is only 23. */
    for (size_t i = 0; i < len; ++i) {
        if (p->used == sizeof(p->data))
            consume(p, 1);
        p->data[p->used++] = data[i];
        for (;;) {
            if (p->used < 2)
                break;
            if (p->data[0] != 0xa5 || p->data[1] != 0x5a) {
                consume(p, 1);
                continue;
            }
            if (p->used < PC_HEADER)
                break;
            size_t n = ((size_t)p->data[4] << 8) | p->data[5];
            if (p->data[2] != PC_VERSION || n > PC_PAYLOAD_MAX) {
                consume(p, 1);
                continue;
            }
            if (p->used < PC_HEADER + n)
                break;
            pc_message_t m = {.type = p->data[3],
                              .length = (uint16_t)n,
                              .request = pc_u32(p->data + 6),
                              .stream = pc_u32(p->data + 10),
                              .sequence = (uint16_t)((p->data[14] << 8) | p->data[15]),
                              .payload = p->data + PC_HEADER};
            fn(&m, ctx);
            consume(p, PC_HEADER + n);
        }
    }
}
bool pc_utf8(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n;) {
        uint32_t v = p[i++], min = 0;
        unsigned k = 0;
        if (v < 0x80) {
            if (v < 0x20 || v == 0x7f)
                return false;
            continue;
        }
        if (v >= 0xc2 && v <= 0xdf) {
            v &= 31;
            k = 1;
            min = 0x80;
        } else if (v >= 0xe0 && v <= 0xef) {
            v &= 15;
            k = 2;
            min = 0x800;
        } else if (v >= 0xf0 && v <= 0xf4) {
            v &= 7;
            k = 3;
            min = 0x10000;
        } else
            return false;
        if (n - i < k)
            return false;
        while (k--) {
            if ((p[i] & 0xc0) != 0x80)
                return false;
            v = (v << 6) | (p[i++] & 63);
        }
        if (v < min || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff))
            return false;
    }
    return true;
}
