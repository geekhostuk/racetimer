/* Gate settings, kept in NVS. GPL-3.0-or-later. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char channels[128];      /* CHANNELS arguments, e.g. "R1=5658 R3=5732 R6=5843 R8=5917" */
    uint8_t gain;            /* the C5s' manual RX gain index */
    uint8_t enter_at, exit_at; /* level codes (dBFS = code / 4 - 63.75) */
    uint8_t delta_max;       /* level codes (0.25 dB) */
    int16_t offset;          /* level codes: top - bottom through the centre (calibration) */
    uint16_t pair_window_ms;
    uint16_t pair_wait_ms;
    uint32_t min_lap_ms;
    char sta_ssid[33];       /* empty: access point only */
    char sta_pass[65];
} settings_t;

/* Load the saved settings, or the defaults. */
void settings_init(void);
/* A copy of the settings in use. */
void settings_get(settings_t *out);
/* Use these settings (not saved). */
void settings_set(const settings_t *s);
/* Save the settings in use to NVS. */
bool settings_save(void);
void settings_defaults(settings_t *out);
