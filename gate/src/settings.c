/* GPL-3.0-or-later. */
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "settings.h"

#define NVS_NS "gate"
#define NVS_KEY "cfg"
#define VERSION 1 /* bump when settings_t changes: older blobs are ignored */

typedef struct {
    uint32_t version;
    settings_t s;
} blob_t;

static settings_t cur;
static SemaphoreHandle_t lock;

void settings_defaults(settings_t *out) {
    *out = (settings_t){
        .channels = "R1=5658 R3=5732 R6=5843 R8=5917",
        .gain = 45,
        /* Widscan58's RotorHazard-mode defaults (-32.5 / -37.5 dBFS). */
        .enter_at = 125,
        .exit_at = 105,
        .delta_max = 24, /* 6 dB */
        .offset = 0,
        .pair_window_ms = 75,
        .pair_wait_ms = 1000,
        .min_lap_ms = 5000,
    };
}

void settings_init(void) {
    lock = xSemaphoreCreateMutex();
    settings_defaults(&cur);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    blob_t b;
    size_t len = sizeof(b);
    if (nvs_get_blob(h, NVS_KEY, &b, &len) == ESP_OK && len == sizeof(b) && b.version == VERSION) {
        b.s.channels[sizeof(b.s.channels) - 1] = 0;
        b.s.sta_ssid[sizeof(b.s.sta_ssid) - 1] = 0;
        b.s.sta_pass[sizeof(b.s.sta_pass) - 1] = 0;
        cur = b.s;
    }
    nvs_close(h);
}

void settings_get(settings_t *out) {
    xSemaphoreTake(lock, portMAX_DELAY);
    *out = cur;
    xSemaphoreGive(lock);
}

void settings_set(const settings_t *s) {
    xSemaphoreTake(lock, portMAX_DELAY);
    cur = *s;
    xSemaphoreGive(lock);
}

bool settings_save(void) {
    blob_t b = {.version = VERSION};
    settings_get(&b.s);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_blob(h, NVS_KEY, &b, sizeof(b)) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}
