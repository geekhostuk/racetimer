/* GPL-3.0-or-later. */
#include <string.h>
#include "gate_fusion.h"

void fusion_init(fusion_t *f, const fusion_cfg_t *cfg) {
    memset(f, 0, sizeof(*f));
    f->cfg = *cfg;
}

void fusion_set_cfg(fusion_t *f, const fusion_cfg_t *cfg) { f->cfg = *cfg; }

static void push(fusion_t *f, const fusion_event_t *e) {
    if (f->ev_n == FUSION_EVENTS) { /* drop the oldest */
        f->ev_head = (uint8_t)((f->ev_head + 1) % FUSION_EVENTS);
        f->ev_n--;
    }
    f->ev[(f->ev_head + f->ev_n) % FUSION_EVENTS] = *e;
    f->ev_n++;
}

bool fusion_next(fusion_t *f, fusion_event_t *out) {
    if (!f->ev_n) return false;
    *out = f->ev[f->ev_head];
    f->ev_head = (uint8_t)((f->ev_head + 1) % FUSION_EVENTS);
    f->ev_n--;
    return true;
}

/* Count a VALID pass as a lap unless it comes too soon after the last one. */
static void lap(fusion_t *f, fusion_event_t *e) {
    if (e->verdict != FUSION_VALID) return;
    unsigned ch = e->ch;
    if (f->have_lap[ch]) {
        uint32_t since = e->t_ms - f->lap_ms[ch];
        if ((int32_t)since < 0 || since < f->cfg.min_lap_ms) return;
        e->lap_ms = since;
    }
    e->counted = true;
    f->have_lap[ch] = true;
    f->lap_ms[ch] = e->t_ms;
}

static void single(fusion_t *f, unsigned board, unsigned ch) {
    fusion_pass_t *p = &f->pend[ch][board];
    fusion_event_t e = {.ch = (uint8_t)ch,
                        .verdict = board == FUSION_TOP ? FUSION_SINGLE_TOP : FUSION_SINGLE_BOTTOM,
                        .t_ms = p->t_ms};
    e.peak[board] = p->peak;
    p->pending = false;
    push(f, &e);
}

static void pair(fusion_t *f, unsigned ch) {
    fusion_pass_t *top = &f->pend[ch][FUSION_TOP], *bot = &f->pend[ch][FUSION_BOTTOM];
    int32_t raw = (int32_t)top->peak - (int32_t)bot->peak;
    fusion_event_t e = {.ch = (uint8_t)ch, .t_ms = bot->t_ms + (uint32_t)((int32_t)(top->t_ms - bot->t_ms) / 2)};
    e.peak[FUSION_TOP] = top->peak;
    e.peak[FUSION_BOTTOM] = bot->peak;
    e.delta = (int16_t)(raw - f->cfg.offset);
    e.verdict = e.delta > f->cfg.delta_max ? FUSION_OVER : e.delta < -(int32_t)f->cfg.delta_max ? FUSION_UNDER : FUSION_VALID;
    top->pending = bot->pending = false;
    if (f->cal_left) {
        e.calibration = true;
        f->cal_sum += raw;
        f->cal_n++;
        if (!--f->cal_left) {
            int32_t n = f->cal_n, s = f->cal_sum;
            f->cfg.offset = (int16_t)(s >= 0 ? (s + n / 2) / n : -((-s + n / 2) / n));
        }
    }
    lap(f, &e);
    push(f, &e);
}

void fusion_pass(fusion_t *f, unsigned board, unsigned ch, uint32_t t_ms, uint8_t peak, uint32_t now_ms) {
    if (board >= FUSION_BOARDS || ch >= FUSION_MAX_CH) return;
    fusion_poll(f, now_ms);
    fusion_pass_t *mine = &f->pend[ch][board], *other = &f->pend[ch][!board];
    if (mine->pending) single(f, board, ch); /* a second pass before the other board's: the first had no pair */
    *mine = (fusion_pass_t){.pending = true, .t_ms = t_ms, .arr_ms = now_ms, .peak = peak};
    if (other->pending) {
        int32_t d = (int32_t)(t_ms - other->t_ms);
        if (d < 0) d = -d;
        if ((uint32_t)d <= f->cfg.pair_window_ms) pair(f, ch);
        else if ((int32_t)(t_ms - other->t_ms) > 0) single(f, !board, ch); /* the other is older and unmatched */
    }
}

void fusion_poll(fusion_t *f, uint32_t now_ms) {
    /* Singles go out oldest first, so laps stay in time order. */
    for (;;) {
        fusion_pass_t *oldest = NULL;
        unsigned ob = 0, oc = 0;
        for (unsigned ch = 0; ch < FUSION_MAX_CH; ch++)
            for (unsigned b = 0; b < FUSION_BOARDS; b++) {
                fusion_pass_t *p = &f->pend[ch][b];
                if (!p->pending || now_ms - p->arr_ms < f->cfg.pair_wait_ms) continue;
                if (!oldest || (int32_t)(p->t_ms - oldest->t_ms) < 0) {
                    oldest = p;
                    ob = b;
                    oc = ch;
                }
            }
        if (!oldest) return;
        single(f, ob, oc);
    }
}

void fusion_calibrate(fusion_t *f, unsigned n) {
    f->cal_left = (uint8_t)(n > 255 ? 255 : n);
    f->cal_n = 0;
    f->cal_sum = 0;
}

bool fusion_calibrating(const fusion_t *f) { return f->cal_left != 0; }

void fusion_clear(fusion_t *f) {
    memset(f->pend, 0, sizeof(f->pend));
    memset(f->have_lap, 0, sizeof(f->have_lap));
}
