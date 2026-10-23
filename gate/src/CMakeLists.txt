# Widscan58's lap detection and frame layout are used as they are, from the
# submodule (GPL-3.0-or-later, https://github.com/CapnBry/widscan58).
set(w58 "${CMAKE_CURRENT_LIST_DIR}/../../third_party/widscan58/firmware/src")

idf_component_register(SRCS "main.c" "c5_link.c" "gate.c" "gate_fusion.c" "settings.c" "web.c" "${w58}/scan/lap.c"
    INCLUDE_DIRS "." "${w58}/scan" "${w58}/proto"
    EMBED_TXTFILES "../web/index.html"
    REQUIRES esp_driver_uart esp_wifi esp_netif esp_event esp_http_server esp_timer nvs_flash esp_rom)
