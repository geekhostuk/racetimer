/* GPL-3.0-or-later. */
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gate.h"
#include "settings.h"
#include "web.h"

#define AP_PASS "racegate"   /* WPA2 needs 8 or more characters */
#define LEVELS_EVERY_MS 33
#define STATUS_EVERY_MS 1000
#define MAX_CLIENTS 4
#define MAX_PENDING 24 /* queued sends; more are dropped (a slow client) */
/* Station retries: 2 s, doubling to 60 s. Each try scans, which takes the
 * access point off its channel for a moment, so a network that isn't there
 * shouldn't be tried flat out. */
#define RETRY_FIRST_MS 2000
#define RETRY_MAX_MS 60000

static const char *TAG = "web";

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

static httpd_handle_t server;
static atomic_int pending; /* sends queued on the server task */

/* The station (home network) link, for the page. */
typedef enum { NET_OFF, NET_CONNECTING, NET_UP, NET_RETRY } net_state_t;
static const char *const NET_STATE[] = {"off", "connecting", "up", "retry"};
static atomic_int net_state;
static atomic_uint net_ip;     /* esp_ip4_addr_t.addr while NET_UP */
static atomic_int net_reason;  /* wifi_err_reason_t of the last failure */
static esp_timer_handle_t retry_timer;
static uint32_t retry_ms = RETRY_FIRST_MS;

/* Sends run on the server's task (httpd_queue_work), so they never race a
 * client closing. A send blocks the server task until the client takes it
 * (send_wait_timeout), so a client that stops reading (a phone gone to sleep,
 * out of range) is closed on its first failed send, and while sends back up
 * new ones are dropped rather than queued. */
typedef struct {
    size_t len;
    int fd; /* -1: every WebSocket client */
    char text[];
} send_job_t;

static void send_one(int fd, httpd_ws_frame_t *frame) {
    if (httpd_ws_send_frame_async(server, fd, frame) != ESP_OK) httpd_sess_trigger_close(server, fd);
}

static void send_work(void *arg) {
    send_job_t *job = arg;
    httpd_ws_frame_t frame = {.type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)job->text, .len = job->len};
    if (job->fd >= 0) {
        send_one(job->fd, &frame);
    } else {
        size_t n = MAX_CLIENTS + 4;
        int fds[MAX_CLIENTS + 4];
        if (httpd_get_client_list(server, &n, fds) == ESP_OK)
            for (size_t i = 0; i < n; i++)
                if (httpd_ws_get_fd_info(server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) send_one(fds[i], &frame);
    }
    free(job);
    pending--;
}

static void send_text(int fd, const char *text, size_t len) {
    if (!server || !len || pending >= MAX_PENDING) return;
    send_job_t *job = malloc(sizeof(*job) + len);
    if (!job) return;
    job->len = len;
    job->fd = fd;
    memcpy(job->text, text, len);
    pending++;
    if (httpd_queue_work(server, send_work, job) != ESP_OK) {
        free(job);
        pending--;
    }
}

static void send_config(int fd) {
    char buf[512];
    send_text(fd, buf, gate_json_config(buf, sizeof(buf)));
}

/* Join (or leave) the home network in the settings. Runs at boot and when the
 * network or password changes, so a new one is tried straight away. */
static void sta_apply(const settings_t *s) {
    esp_timer_stop(retry_timer);
    retry_ms = RETRY_FIRST_MS;
    net_ip = 0;
    net_reason = 0;
    if (!s->sta_ssid[0]) {
        net_state = NET_OFF;
        esp_wifi_set_mode(WIFI_MODE_AP);
        return;
    }
    wifi_config_t st = {0};
    snprintf((char *)st.sta.ssid, sizeof(st.sta.ssid), "%s", s->sta_ssid);
    snprintf((char *)st.sta.password, sizeof(st.sta.password), "%s", s->sta_pass);
    net_state = NET_CONNECTING;
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_disconnect(); /* from the old network: WIFI_REASON_ASSOC_LEAVE, not retried */
    esp_wifi_set_config(WIFI_IF_STA, &st);
    esp_wifi_connect();
}

static void retry(void *arg) {
    (void)arg;
    net_state = NET_CONNECTING;
    esp_wifi_connect();
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *d = data;
        net_ip = 0;
        if (d->reason == WIFI_REASON_ASSOC_LEAVE) return; /* we left: sta_apply */
        net_reason = d->reason;
        net_state = NET_RETRY;
        ESP_LOGW(TAG, "home network: not connected (reason %u), retry in %" PRIu32 " s", d->reason, retry_ms / 1000);
        esp_timer_start_once(retry_timer, (uint64_t)retry_ms * 1000);
        retry_ms = retry_ms * 2 > RETRY_MAX_MS ? RETRY_MAX_MS : retry_ms * 2;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *g = data;
        net_ip = g->ip_info.ip.addr;
        net_reason = 0;
        net_state = NET_UP;
        retry_ms = RETRY_FIRST_MS;
        ESP_LOGI(TAG, "home network: connected, http://" IPSTR "/", IP2STR(&g->ip_info.ip));
    }
}

static void send_net(int fd) {
    settings_t s;
    settings_get(&s);
    char ssid[2 * sizeof(s.sta_ssid)];
    size_t k = 0;
    for (const char *p = s.sta_ssid; *p && k + 2 < sizeof(ssid); p++) {
        if (*p == '"' || *p == '\\') ssid[k++] = '\\';
        ssid[k++] = *p;
    }
    ssid[k] = 0;
    esp_ip4_addr_t ip = {.addr = net_ip};
    char buf[160];
    int n = snprintf(buf, sizeof(buf), "{\"t\":\"net\",\"st\":\"%s\",\"ssid\":\"%s\",\"ip\":\"" IPSTR "\",\"why\":%d}",
                     NET_STATE[net_state], ssid, IP2STR(&ip), net_reason);
    if (n > 0 && (size_t)n < sizeof(buf)) send_text(fd, buf, (size_t)n);
}

static void apply(const settings_t *s) {
    settings_t old;
    settings_get(&old);
    settings_set(s);
    gate_settings_changed(&old, s);
    send_config(-1);
}

static void command(int fd, char *line) {
    settings_t s;
    settings_get(&s);
    char key[16];
    int pos = 0;
    long v = 0;
    if (sscanf(line, "set %15s %n", key, &pos) == 1 && pos) {
        const char *val = line + pos;
        v = strtol(val, NULL, 10);
        if (!strcmp(key, "gain") && v >= 0 && v <= 83) s.gain = (uint8_t)v;
        else if (!strcmp(key, "enter") && v >= 1 && v <= 254) s.enter_at = (uint8_t)v;
        else if (!strcmp(key, "exit") && v >= 1 && v <= 254) s.exit_at = (uint8_t)v;
        else if (!strcmp(key, "delta") && v >= 0 && v <= 254) s.delta_max = (uint8_t)v;
        else if (!strcmp(key, "offset") && v >= -254 && v <= 254) s.offset = (int16_t)v;
        else if (!strcmp(key, "pair") && v >= 1 && v <= 1000) s.pair_window_ms = (uint16_t)v;
        else if (!strcmp(key, "wait") && v >= 50 && v <= 10000) s.pair_wait_ms = (uint16_t)v;
        else if (!strcmp(key, "minlap") && v >= 0 && v <= 600000) s.min_lap_ms = (uint32_t)v;
        else if (!strcmp(key, "channels") && *val) snprintf(s.channels, sizeof(s.channels), "%s", val);
        else if (!strcmp(key, "ssid")) snprintf(s.sta_ssid, sizeof(s.sta_ssid), "%s", val);
        else if (!strcmp(key, "pass")) snprintf(s.sta_pass, sizeof(s.sta_pass), "%s", val);
        else {
            gate_line(C5_BOARDS, "ERR gate: bad setting");
            return;
        }
        if (s.exit_at > s.enter_at) s.exit_at = s.enter_at;
        settings_t old;
        settings_get(&old);
        apply(&s);
        if (strcmp(old.sta_ssid, s.sta_ssid) || strcmp(old.sta_pass, s.sta_pass)) sta_apply(&s);
    } else if (!strcmp(line, "save")) {
        gate_line(C5_BOARDS, settings_save() ? "OK gate: saved" : "ERR gate: save failed");
    } else if (!strcmp(line, "defaults")) {
        settings_t d;
        settings_defaults(&d);
        snprintf(d.sta_ssid, sizeof(d.sta_ssid), "%s", s.sta_ssid); /* keep the way in */
        snprintf(d.sta_pass, sizeof(d.sta_pass), "%s", s.sta_pass);
        apply(&d);
    } else if (sscanf(line, "cal %ld", &v) == 1 && v >= 1 && v <= 50) {
        gate_calibrate((unsigned)v);
    } else if (sscanf(line, "baud %ld %n", &v, &pos) == 1 && pos && v >= 0 && v < C5_BOARDS) {
        c5_link_set_baud((unsigned)v, atoi(line + pos)); /* 0: back to the link's own */
    } else if (!strcmp(line, "clear")) {
        gate_clear();
    } else if (!strncmp(line, "c5 ", 3)) {
        char who[8];
        if (sscanf(line + 3, "%7s %n", who, &pos) == 1 && pos) {
            const char *cmd = line + 3 + pos;
            for (unsigned b = 0; b < C5_BOARDS; b++)
                if (!strcmp(who, "all") || (unsigned)atoi(who) == b) c5_link_send(b, cmd);
        }
    } else {
        gate_line(C5_BOARDS, "ERR gate: unknown command");
    }
    (void)fd;
}

static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) { /* the handshake */
        send_config(httpd_req_to_sockfd(req));
        send_net(httpd_req_to_sockfd(req));
        return ESP_OK;
    }
    httpd_ws_frame_t frame = {0};
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) return err;
    if (frame.type != HTTPD_WS_TYPE_TEXT || frame.len == 0 || frame.len > 255) return ESP_OK;
    char buf[256];
    frame.payload = (uint8_t *)buf;
    err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err != ESP_OK) return err;
    buf[frame.len] = 0;
    command(httpd_req_to_sockfd(req), buf);
    return ESP_OK;
}

static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    /* EMBED_TXTFILES adds a terminating zero; leave it out. */
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start - 1);
}

static void ui_task(void *arg) {
    (void)arg;
    static char buf[768];
    TickType_t wake = xTaskGetTickCount();
    uint32_t status_ms = 0;
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(LEVELS_EVERY_MS));
        size_t n = gate_json_levels(buf, sizeof(buf));
        send_text(-1, buf, n);
        while ((n = gate_json_event(buf, sizeof(buf))) > 0) send_text(-1, buf, n);
        uint32_t now = gate_now_ms();
        if (now - status_ms >= STATUS_EVERY_MS) {
            status_ms = now;
            send_text(-1, buf, gate_json_status(buf, sizeof(buf)));
            send_config(-1); /* a calibration changes the offset */
            send_net(-1);
        }
    }
}

static void wifi_start(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    esp_timer_create_args_t t = {.callback = retry, .name = "sta_retry"};
    ESP_ERROR_CHECK(esp_timer_create(&t, &retry_timer));

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    wifi_config_t ap = {.ap = {.channel = 1, .max_connection = MAX_CLIENTS, .authmode = WIFI_AUTH_WPA2_PSK}};
    int len = snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "racegate-%02X%02X", mac[4], mac[5]);
    ap.ap.ssid_len = (uint8_t)len;
    snprintf((char *)ap.ap.password, sizeof(ap.ap.password), "%s", AP_PASS);
    esp_netif_set_hostname(sta_netif, (char *)ap.ap.ssid); /* the name the router lists */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());

    settings_t s;
    settings_get(&s);
    sta_apply(&s);
}

void web_start(void) {
    wifi_start();
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_open_sockets = MAX_CLIENTS + 2;
    cfg.lru_purge_enable = true;
    cfg.send_wait_timeout = 1; /* s; a client that stops reading is closed (send_one) */
    ESP_ERROR_CHECK(httpd_start(&server, &cfg));
    httpd_uri_t index = {.uri = "/", .method = HTTP_GET, .handler = index_handler};
    httpd_uri_t ws = {.uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .is_websocket = true};
    httpd_register_uri_handler(server, &index);
    httpd_register_uri_handler(server, &ws);
    xTaskCreatePinnedToCore(ui_task, "ui", 4096, NULL, 5, NULL, 0);
}
