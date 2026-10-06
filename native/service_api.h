/* Luna API handlers. Included after the shared runtime context. */
#include "service_calibration.h"
static bool start_failure(void *h, void *m, const char *message) {
    retry_update(&retries, boot_enabled() && geteuid() == 0, false, milliseconds());
    char json[512];
    snprintf(
        json, sizeof(json),
        "{\"returnValue\":false,\"errorText\":\"%s\",\"retryScheduled\":%s,\"retryBlocked\":%s}",
        message, retries.due ? "true" : "false", retries.blocked ? "true" : "false");
    return respond(h, m, json);
}
static bool start(void *h, void *m, void *ctx) {
    (void)ctx;
    const char *p = m ? payload(m) : NULL;
    if (p && strcmp(p, "{}") != 0)
        return respond(h, m,
                       "{\"returnValue\":false,\"errorText\":\"Start expects an empty object; use "
                       "settings to configure the receiver\"}");
    if (m)
        retry_reset(&retries);
    if (piccap_owned)
        return respond(h, m, "{\"returnValue\":true,\"running\":true,\"activeSource\":\"PicCap\"}");
    if (capture)
        return respond(h, m, "{\"returnValue\":true,\"running\":true}");
    frames = 0;
    revoked = 0;
    last_error[0] = 0;
    lg_diagnostic[0] = 0;
    output = target_connect(&target);
    if (output < 0) {
        stop_capture();
        set_error("Cannot connect to configured receiver IP and JSON port");
        return start_failure(h, m, "Configured receiver is unavailable");
    }
    if (!target_login()) {
        stop_capture();
        set_error("Receiver token login failed; check the saved API token");
        return start_failure(h, m, "Receiver authentication failed");
    }
    if (!read_layout()) {
        stop_capture();
        set_error("Cannot read receiver LED layout; check JSON port and API authorization");
        return start_failure(h, m, "Receiver layout unavailable; check API authorization");
    }
    if (!enter_developer_mode()) {
        stop_capture();
        set_error("Developer initialization needs root. Elevate this own service through Homebrew "
                  "Channel.");
        return start_failure(h, m,
                             "Root access is required for temporary LG developer initialization");
    }
    capture = vt_init(policy);
    if (!leave_developer_mode()) {
        stop_capture();
        retries.blocked = true;
        set_error("Developer marker cleanup failed; manual intervention required");
        return start_failure(h, m, "Developer marker cleanup failed");
    }
    if (!capture) {
        stop_capture();
        set_error("LG refused histogram initialization");
        return start_failure(
            h, m,
            "LG refused histogram initialization. Another capture source may hold VT_CAPTURE.");
    }
    if (vt_preset(capture, 0) != 0) {
        stop_capture();
        set_error("LG histogram preset initialization failed");
        return start_failure(h, m, "Histogram preset failed");
    }
    capture_preset = letterbox_mode = settling = 0;
    pending_letterbox = -1;
    next_border_probe = 0;
    retries.due = 0;
    retries.attempts = 0;
    retries.quick_retries = 0;
    if (recovery_started) {
        last_recovery_ms = milliseconds() - recovery_started;
        recovery_started = 0;
    }
    return respond(h, m, "{\"returnValue\":true,\"running\":true}");
}
static bool stop(void *h, void *m, void *ctx) {
    (void)ctx;
    calibration_until = 0;
    retry_pause(&retries);
    bool bridging = bridge_phase != BRIDGE_IDLE;
    stream_close(milliseconds());
    bridge_phase = BRIDGE_IDLE;
    memset(&continuity, 0, sizeof(continuity));
    stop_capture();
    if (bridging) {
        output = target_connect(&target);
        if (output >= 0) {
            if (target_login())
                send_line("{\"command\":\"clear\",\"priority\":149}\n");
            close(output);
            output = -1;
        }
    }
    return respond(h, m, "{\"returnValue\":true,\"running\":false}");
}
static bool autostart(void *h, void *m, void *ctx) {
    (void)ctx;
    const char *p = m ? payload(m) : NULL;
    bool enabled;
    if (p && strcmp(p, "{\"enabled\":true}") == 0)
        enabled = true;
    else if (p && strcmp(p, "{\"enabled\":false}") == 0)
        enabled = false;
    else
        return respond(h, m, "{\"returnValue\":false,\"errorText\":\"Expected enabled boolean\"}");
    if (geteuid() != 0)
        return respond(
            h, m,
            "{\"returnValue\":false,\"rootRequired\":true,\"errorText\":\"Root access is required "
            "to change autostart. Select Enable Root Access in Overview.\"}");
    if (!set_boot(enabled))
        return respond(
            h, m,
            "{\"returnValue\":false,\"errorText\":\"Cannot update the HyperDRM boot hook. Check "
            "the installed boot script and whether another file occupies the hook path.\"}");
    if (enabled)
        retry_reset(&retries);
    retry_update(&retries, enabled, capture != NULL, milliseconds());
    return respond(h, m,
                   enabled ? "{\"returnValue\":true,\"autostart\":true}"
                           : "{\"returnValue\":true,\"autostart\":false}");
}
static bool restart_service(void *h, void *m, void *ctx) {
    (void)ctx;
    if (strcmp(payload(m), "{}"))
        return respond(h, m,
                       "{\"returnValue\":false,\"errorText\":\"Restart expects an empty object\"}");
    bool ok = respond(h, m, "{\"returnValue\":true,\"restarting\":true}");
    quitting = 1;
    return ok;
}
static bool handover(void *h, void *m, void *ctx) {
    (void)ctx;
    const char *p = payload(m);
    bool enabled;
    if (strcmp(p, "{\"enabled\":true}") == 0)
        enabled = true;
    else if (strcmp(p, "{\"enabled\":false}") == 0)
        enabled = false;
    else
        return respond(h, m, "{\"returnValue\":false,\"errorText\":\"Expected enabled boolean\"}");
    if (!playback_json.lib || geteuid() != 0)
        return respond(
            h, m, "{\"returnValue\":false,\"errorText\":\"Root service and JSON-C are required\"}");
    if (enabled && !piccap_installed())
        return respond(h, m, "{\"returnValue\":false,\"errorText\":\"PicCap is not installed\"}");
    if (!handover_save(enabled))
        return respond(
            h, m,
            "{\"returnValue\":false,\"errorText\":\"Could not save PicCap handover preference\"}");
    handover_enabled = enabled;
    protection = PLAYBACK_UNKNOWN;
    clear_samples = 0;
    clear_since = unknown_since = 0;
    next_media_probe = 0;
    return respond(h, m, "{\"returnValue\":true}");
}
static bool settings(void *h, void *m, void *ctx) {
    (void)ctx;
    TargetConfig next = target;
    if (!playback_json.lib || !target_parse(&playback_json, payload(m), &next))
        return respond(h, m,
                       "{\"returnValue\":false,\"errorText\":\"Enter a valid IPv4/IPv6 address, "
                       "integer port (1-65535), receiver and optional API token\"}");
    if (!target_save(TARGET_PREF, &next))
        return respond(
            h, m, "{\"returnValue\":false,\"errorText\":\"Could not save receiver settings\"}");
    bool active = capture || piccap_owned || retries.due;
    stop_capture();
    stream_close(milliseconds());
    bridge_phase = BRIDGE_IDLE;
    memset(&continuity, 0, sizeof(continuity));
    test_close();
    connection_test.stage = TEST_IDLE;
    target = next;
    last_error[0] = 0;
    protection = PLAYBACK_UNKNOWN;
    clear_samples = 0;
    clear_since = unknown_since = 0;
    if (active && !retries.paused && !retries.blocked)
        retries.due = milliseconds();
    return respond(h, m, "{\"returnValue\":true}");
}
static bool test_connection(void *h, void *m, void *ctx) {
    (void)ctx;
    if (strcmp(payload(m), "{}") || !playback_json.lib)
        return respond(
            h, m,
            "{\"returnValue\":false,\"errorText\":\"Test requires an empty object and JSON-C\"}");
    if (!test_begin(&target, milliseconds()))
        return respond(
            h, m, "{\"returnValue\":false,\"errorText\":\"A connection test is already running\"}");
    return respond(h, m, "{\"returnValue\":true,\"testing\":true}");
}
#include "service_status.h"
