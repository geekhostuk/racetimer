/* The gate: both receivers' levels in, per-board pass detection (Widscan58's
 * lap.c), the fusion's verdicts out, and what the web UI shows.
 * GPL-3.0-or-later.
 *
 * Times: each C5 stamps its LEVELS frames with its own ms clock. The gate
 * puts them on its own clock (esp_timer) with a per-board offset, the
 * smallest (arrival - board time) over the last 1-2 s: the board's clock
 * plus the shortest link delay seen, good to about a millisecond. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "c5_link.h"
#include "settings.h"

#define GATE_MAX_CH 8

void gate_init(void);
/* New settings in use (settings_set has run): re-set up the boards if the
 * channels or gain changed, and apply thresholds and fusion limits. */
void gate_settings_changed(const settings_t *old, const settings_t *now);
/* c5_link callbacks. */
void gate_levels(unsigned board, const frame_levels_t *h, const frame_level_t *lv, int64_t rx_us);
void gate_line(unsigned board, const char *line);
void gate_telemetry(unsigned board, const frame_telemetry_t *t);
/* Average the next n paired passes into the top/bottom offset. */
void gate_calibrate(unsigned n);
/* Forget laps. */
void gate_clear(void);
/* The gate's clock, ms. */
uint32_t gate_now_ms(void);

/* JSON messages for the web UI. Each returns the length written (0: none). */
/* Each channel's highest level per board since the previous call. */
size_t gate_json_levels(char *buf, size_t cap);
/* The oldest queued event (a pass verdict, one board's pass, a reply line). */
size_t gate_json_event(char *buf, size_t cap);
/* Board health and calibration state. */
size_t gate_json_status(char *buf, size_t cap);
/* The settings in use and the channel names. */
size_t gate_json_config(char *buf, size_t cap);
