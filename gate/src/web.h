/* Wi-Fi (access point, plus a station if one is set) and the web UI:
 * index.html and one WebSocket, /ws. GPL-3.0-or-later.
 *
 * To the page: JSON messages from gate.h (levels ~30 times a second,
 * events as they happen, status and config once a second).
 * From the page, text commands:
 *   set <key> <value>   gain, enter, exit, delta, offset, pair, wait, minlap,
 *                       channels (the rest of the line), ssid, pass
 *   save                keep the settings in use over a reboot
 *   defaults            the built-in settings (not saved)
 *   cal <n>             calibrate the offset from the next n paired passes
 *   clear               forget laps
 *   c5 <0|1|all> <cmd>  a command line to the receivers, replies come back
 *                       as "line" events */
#pragma once

void web_start(void);
