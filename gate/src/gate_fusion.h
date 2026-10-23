/* Pairs the passes of a gate's two receivers (top bar, bottom bar) and
 * decides whether the quad flew through the gate. Target independent (host
 * tested, test/test_fusion). GPL-3.0-or-later.
 *
 * Each receiver detects passes on its own (Widscan58's lap.c). A pass from
 * one board waits for the other board's pass on the same channel: two passes
 * whose times are within pair_window_ms are one gate crossing, and
 * delta = (top peak - bottom peak) - offset, in level codes (0.25 dB),
 * decides it:
 *   |delta| <= delta_max  VALID, through the gate
 *   delta > delta_max     OVER, much stronger on the top bar
 *   delta < -delta_max    UNDER
 * A pass the other board doesn't match within pair_wait_ms of its arrival
 * is SINGLE_TOP or SINGLE_BOTTOM: only one bar saw it above EnterAt, which
 * also means over or under. Only VALID passes count as laps, and only
 * min_lap_ms or more after the channel's previous lap.
 *
 * Pass times are the receivers' pass times (the middle of the peak) on the
 * gate's clock; arrival times are when lap.c reported them, which is when
 * the level fell below ExitAt, so a weaker receiver can report much sooner
 * than a stronger one. All times are ms on one free-running clock (wraps). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define FUSION_MAX_CH 8
#define FUSION_EVENTS 16 /* queued events; the oldest is dropped when full */

enum { FUSION_TOP = 0, FUSION_BOTTOM = 1, FUSION_BOARDS = 2 };

typedef enum {
    FUSION_VALID,
    FUSION_OVER,
    FUSION_UNDER,
    FUSION_SINGLE_TOP,
    FUSION_SINGLE_BOTTOM,
} fusion_verdict_t;

typedef struct {
    uint16_t pair_window_ms; /* most the two pass times may differ */
    uint16_t pair_wait_ms;   /* how long a pass waits for the other board's */
    uint8_t delta_max;       /* level codes (0.25 dB) */
    int16_t offset;          /* level codes: top - bottom for a pass through the centre */
    uint32_t min_lap_ms;
} fusion_cfg_t;

typedef struct {
    uint8_t ch;
    uint8_t verdict;         /* fusion_verdict_t */
    bool counted;            /* a lap: VALID and not too soon after the last */
    bool calibration;        /* used for a calibration in progress */
    uint32_t t_ms;           /* pass time: the mean of the pair, or the single pass */
    uint8_t peak[FUSION_BOARDS]; /* level codes; 0 for the board that missed it */
    int16_t delta;           /* level codes, offset applied; 0 for a single */
    uint32_t lap_ms;         /* counted: time since the channel's previous lap; 0 for the first */
} fusion_event_t;

typedef struct {
    bool pending;
    uint32_t t_ms;   /* pass time */
    uint32_t arr_ms; /* when it arrived */
    uint8_t peak;
} fusion_pass_t;

typedef struct {
    fusion_cfg_t cfg;
    fusion_pass_t pend[FUSION_MAX_CH][FUSION_BOARDS];
    bool have_lap[FUSION_MAX_CH];
    uint32_t lap_ms[FUSION_MAX_CH]; /* time of the channel's last lap */
    /* Calibration: the next cal_left pairs' raw top - bottom, averaged into
     * cfg.offset when the last one arrives. */
    uint8_t cal_left, cal_n;
    int32_t cal_sum;
    fusion_event_t ev[FUSION_EVENTS];
    uint8_t ev_head, ev_n;
} fusion_t;

void fusion_init(fusion_t *f, const fusion_cfg_t *cfg);
/* New settings; pending passes and laps are kept. */
void fusion_set_cfg(fusion_t *f, const fusion_cfg_t *cfg);
/* One receiver's pass, arriving at now_ms. */
void fusion_pass(fusion_t *f, unsigned board, unsigned ch, uint32_t t_ms, uint8_t peak, uint32_t now_ms);
/* Report passes that waited pair_wait_ms for their pair as singles. */
void fusion_poll(fusion_t *f, uint32_t now_ms);
/* The oldest event; false if none. */
bool fusion_next(fusion_t *f, fusion_event_t *out);
/* Average the next n pairs (whatever their verdict) into cfg.offset. */
void fusion_calibrate(fusion_t *f, unsigned n);
bool fusion_calibrating(const fusion_t *f);
/* Forget the laps (the next VALID pass on each channel is a first lap) and
 * any pending pass. */
void fusion_clear(fusion_t *f);
