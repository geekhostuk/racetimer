/* racetimer gate controller: two Widscan58 receivers (top and bottom bar of
 * one gate) on UARTs, pass validation, and a web UI. GPL-3.0-or-later.
 *
 * Wiring (docs/hardware.md): each C5's TX to the WROOM's RX for its UART
 * and back, common ground. */
#include "driver/uart.h"
#include "nvs_flash.h"
#include "c5_link.h"
#include "gate.h"
#include "gate_fusion.h"
#include "settings.h"
#include "web.h"

#define C5_BAUD 2000000 /* Widscan58 env esp32c5_uart */
/* Top bar receiver: UART2 on its usual pins. */
#define TOP_UART UART_NUM_2
#define TOP_RX 16
#define TOP_TX 17
/* Bottom bar receiver: UART1, moved off the flash pins. */
#define BOTTOM_UART UART_NUM_1
#define BOTTOM_RX 26
#define BOTTOM_TX 27

void app_main(void) {
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        e = nvs_flash_init();
    }
    ESP_ERROR_CHECK(e);
    settings_init();
    c5_link_init(gate_levels, gate_line, gate_telemetry); /* before gate_init, which configures the link */
    gate_init();
    c5_link_start(FUSION_TOP, TOP_UART, TOP_TX, TOP_RX, C5_BAUD);
    c5_link_start(FUSION_BOTTOM, BOTTOM_UART, BOTTOM_TX, BOTTOM_RX, C5_BAUD);
    web_start();
}
