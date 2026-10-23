/* GPL-3.0-or-later. */
#include <stdio.h>
#include <string.h>
#include "driver/uart.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "c5_link.h"

#define RX_BUF 8192           /* ~45 ms of LEVELS frames at 2 Mbaud */
#define MAX_PAYLOAD 1024      /* LEVELS, TELEMETRY, LOG; we never ask for spectra or raw I/Q */
#define LINE_BYTES 256
#define SILENT_US 1500000     /* no LEVELS this long: set the board up again */
#define SETUP_EVERY_US 3000000 /* at most this often */
#define SETUP_GAP_MS 30       /* between setup commands */

typedef struct {
    int uart;
    bool started;
    volatile bool setup_now;
    int64_t last_setup_us;
    /* Parser */
    enum { P_IDLE, P_HEADER, P_PAYLOAD } state;
    uint8_t frame[sizeof(frame_header_t) + MAX_PAYLOAD + 4];
    size_t got, want;
    char line[LINE_BYTES];
    size_t line_len;
    c5_stats_t stats;
} board_t;

static board_t boards[C5_BOARDS];
static c5_levels_cb_t on_levels;
static c5_line_cb_t on_line;
static c5_telemetry_cb_t on_telemetry;
static SemaphoreHandle_t cfg_lock;
static char cfg_channels[128] = "R1=5658 R3=5732 R6=5843 R8=5917";
static uint8_t cfg_gain = 45;

void c5_link_init(c5_levels_cb_t levels, c5_line_cb_t line, c5_telemetry_cb_t telemetry) {
    on_levels = levels;
    on_line = line;
    on_telemetry = telemetry;
    cfg_lock = xSemaphoreCreateMutex();
}

void c5_link_configure(const char *channels, uint8_t gain) {
    xSemaphoreTake(cfg_lock, portMAX_DELAY);
    snprintf(cfg_channels, sizeof(cfg_channels), "%s", channels);
    cfg_gain = gain;
    xSemaphoreGive(cfg_lock);
}

void c5_link_send(unsigned board, const char *line) {
    if (board >= C5_BOARDS || !boards[board].started) return;
    uart_write_bytes(boards[board].uart, line, strlen(line));
    uart_write_bytes(boards[board].uart, "\n", 1);
}

void c5_link_setup(unsigned board) {
    if (board < C5_BOARDS) boards[board].setup_now = true;
}

void c5_link_stats(unsigned board, c5_stats_t *out) {
    if (board < C5_BOARDS) *out = boards[board].stats;
}

/* PROTOCOL MONITOR first: it resets the metrics to FULL. The capture and FFT
 * are Widscan58's RotorHazard-mode defaults (4 x 64-point FFT, 1.25 MHz bins). */
static void send_setup(unsigned board) {
    char channels[sizeof(cfg_channels)], line[160];
    uint8_t gain;
    xSemaphoreTake(cfg_lock, portMAX_DELAY);
    memcpy(channels, cfg_channels, sizeof(channels));
    gain = cfg_gain;
    xSemaphoreGive(cfg_lock);
    snprintf(line, sizeof(line), "CHANNELS %s", channels);
    char gain_line[24];
    snprintf(gain_line, sizeof(gain_line), "GAIN MANUAL %u", gain);
    const char *cmds[] = {"PROTOCOL MONITOR", "SCAN STOP", line, "MODE GROUP", "FFT 64", "CAPN 256",
                          "SPEC 0 256 40000000 MAX", "METRICS RSSI", gain_line, "SCAN START"};
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        c5_link_send(board, cmds[i]);
        vTaskDelay(pdMS_TO_TICKS(SETUP_GAP_MS));
    }
    boards[board].stats.setups++;
    boards[board].last_setup_us = esp_timer_get_time();
}

static void frame_done(unsigned board, int64_t now) {
    board_t *b = &boards[board];
    const frame_header_t *h = (const frame_header_t *)b->frame;
    size_t n = sizeof(*h) + h->len;
    uint32_t crc;
    memcpy(&crc, b->frame + n, 4);
    if (esp_rom_crc32_le(0, b->frame, n) != crc) {
        b->stats.crc_errors++;
        return;
    }
    b->stats.frames++;
    const uint8_t *p = b->frame + sizeof(*h);
    if (h->type == FRAME_LEVELS && h->len >= sizeof(frame_levels_t)) {
        const frame_levels_t *lh = (const frame_levels_t *)p;
        if (h->len < sizeof(*lh) + lh->nch * sizeof(frame_level_t)) {
            b->stats.bad_frames++;
            return;
        }
        b->stats.levels++;
        b->stats.last_levels_us = now;
        if (on_levels) on_levels(board, lh, (const frame_level_t *)(p + sizeof(*lh)), now);
    } else if (h->type == FRAME_TELEMETRY && h->len >= sizeof(frame_telemetry_t)) {
        if (on_telemetry) on_telemetry(board, (const frame_telemetry_t *)p);
    }
}

/* Lines and frames come whole (the board queues each as one item), so a
 * frame starts at a 0xA5 outside a line. A bad header is skipped byte by
 * byte until the next sync. */
static void parse(unsigned board, const uint8_t *buf, int n, int64_t now) {
    board_t *b = &boards[board];
    for (int i = 0; i < n; i++) {
        uint8_t c = buf[i];
        switch (b->state) {
        case P_IDLE:
            if (c == FRAME_SYNC0) {
                b->frame[0] = c;
                b->got = 1;
                b->state = P_HEADER;
                b->line_len = 0;
            } else if (c == '\n') {
                b->line[b->line_len] = 0;
                if (b->line_len && on_line) on_line(board, b->line);
                b->line_len = 0;
            } else if (c >= 0x20 && c < 0x7f && b->line_len + 1 < LINE_BYTES) {
                b->line[b->line_len++] = (char)c;
            }
            break;
        case P_HEADER:
            b->frame[b->got++] = c;
            if (b->got == sizeof(frame_header_t)) {
                const frame_header_t *h = (const frame_header_t *)b->frame;
                if (h->sync1 != FRAME_SYNC1 || h->version != PROTO_VERSION || h->len > MAX_PAYLOAD) {
                    b->stats.bad_frames++;
                    b->state = P_IDLE;
                    break;
                }
                b->want = sizeof(frame_header_t) + h->len + 4;
                b->state = P_PAYLOAD;
            }
            break;
        case P_PAYLOAD:
            b->frame[b->got++] = c;
            if (b->got == b->want) {
                frame_done(board, now);
                b->state = P_IDLE;
            }
            break;
        }
    }
}

static void link_task(void *arg) {
    unsigned board = (unsigned)(uintptr_t)arg;
    board_t *b = &boards[board];
    static uint8_t bufs[C5_BOARDS][512];
    uint8_t *buf = bufs[board];
    for (;;) {
        int n = uart_read_bytes(b->uart, buf, 512, pdMS_TO_TICKS(10));
        int64_t now = esp_timer_get_time();
        if (n > 0) parse(board, buf, n, now);
        bool silent = now - b->stats.last_levels_us > SILENT_US;
        if (b->setup_now || (silent && now - b->last_setup_us > SETUP_EVERY_US)) {
            b->setup_now = false;
            send_setup(board);
        }
    }
}

void c5_link_start(unsigned board, int uart, int tx_pin, int rx_pin, int baud) {
    board_t *b = &boards[board];
    uart_config_t cfg = {
        .baud_rate = baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(uart, RX_BUF, 1024, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(uart, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(uart, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    b->uart = uart;
    b->started = true;
    /* The board may still be booting: wait for its first silence check. */
    b->last_setup_us = esp_timer_get_time() - SETUP_EVERY_US + 500000;
    char name[12];
    snprintf(name, sizeof(name), "c5link%u", board);
    xTaskCreatePinnedToCore(link_task, name, 4096, (void *)(uintptr_t)board, 10, NULL, 1);
}
