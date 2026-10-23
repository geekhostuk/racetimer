/* The UART link to one Widscan58 receiver (ESP32-C5, firmware env
 * esp32c5_uart). GPL-3.0-or-later.
 *
 * Widscan58's monitor protocol: ASCII command lines to the board; ASCII
 * reply lines and binary frames back (frames.h:
 * 0xA5 'R' type version len payload crc32). The link sets the board up for
 * lap timing (c5_link_setup: our channels, METRICS RSSI, no spectrum) and
 * again whenever the LEVELS frames stop, e.g. after the board reboots. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "frames.h"

#define C5_BOARDS 2

/* One LEVELS frame (one hop: the channels on one LO). rx_us: when its last
 * byte arrived, on esp_timer's clock. */
typedef void (*c5_levels_cb_t)(unsigned board, const frame_levels_t *h, const frame_level_t *lv, int64_t rx_us);
/* One reply line from the board, without the newline. */
typedef void (*c5_line_cb_t)(unsigned board, const char *line);
/* One TELEMETRY frame (about once a second). */
typedef void (*c5_telemetry_cb_t)(unsigned board, const frame_telemetry_t *t);

typedef struct {
    uint32_t frames, levels, crc_errors, bad_frames, setups;
    int64_t last_levels_us;
} c5_stats_t;

void c5_link_init(c5_levels_cb_t levels, c5_line_cb_t line, c5_telemetry_cb_t telemetry);
/* Start the link on a UART. */
void c5_link_start(unsigned board, int uart, int tx_pin, int rx_pin, int baud);
/* What c5_link_setup sends: the channel plan (CHANNELS arguments, e.g.
 * "R1=5658 R3=5732") and the manual RX gain. Takes effect on the next setup. */
void c5_link_configure(const char *channels, uint8_t gain);
/* Listen at another baud rate (0: back to the link's own), e.g. 115200 to
 * read the board's boot ROM messages after a reset. No setups meanwhile. */
void c5_link_set_baud(unsigned board, int baud);
/* Send the setup now (from the link's own task, soon). */
void c5_link_setup(unsigned board);
/* One command line, without the newline. */
void c5_link_send(unsigned board, const char *line);
void c5_link_stats(unsigned board, c5_stats_t *out);
