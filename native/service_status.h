/* Read-only runtime status serialization. */
static bool status(void *h, void *m, void *ctx) {
    (void)ctx;
    char json[8192], safe[512], endpoint[80];
    size_t n = 0;
    snprintf(endpoint, sizeof(endpoint), strchr(target.host, ':') ? "[%s]:%u" : "%s:%u",
             target.host, target.port);
    for (size_t i = 0; last_error[i] && n + 2 < sizeof(safe); i++) {
        char c = last_error[i];
        if (c == '"' || c == '\\')
            safe[n++] = '\\';
        if ((unsigned char)c >= 32)
            safe[n++] = c;
    }
    safe[n] = 0;
    uint64_t now = milliseconds();
    char report[512];
    size_t k = 0;
    for (size_t i = 0; calibration_message[i] && k + 2 < sizeof(report); i++) {
        char c = calibration_message[i];
        if (c == '"' || c == '\\')
            report[k++] = '\\';
        if ((unsigned char)c >= 32)
            report[k++] = c;
    }
    report[k] = 0;
    int used = snprintf(json, sizeof(json),
                        "{\"calibrationProfile\":\"%s\",\"effectiveCalibration\":\"%s\","
                        "\"tvModel\":\"%s\",\"customModel\":\"%s\",\"customPoints\":[",
                        calibration_key(calibration.kind), calibration_key(calibration_effective()),
                        tv_model, calibration.model);
    for (unsigned i = 0; i < calibration.count; i++)
        used +=
            snprintf(json + used, sizeof(json) - used, "%s{\"position\":%.12g,\"region\":%u}",
                     i ? "," : "", calibration.points[i].position, calibration.points[i].region);
    used += snprintf(json + used, sizeof(json) - used,
                     "],\"layoutUpdates\":%u,\"lastLayoutCheckAgeMs\":%lld,\"layoutError\":\"%s\","
                     "\"piccapFallbacks\":%u,\"piccapCooldownMs\":%llu,",
                     layout_updates, last_layout_check ? (long long)(now - last_layout_check) : -1,
                     layout_error, piccap_fallbacks,
                     (unsigned long long)(handover_cooldown > now ? handover_cooldown - now : 0));
    used += snprintf(json + used, sizeof(json) - used, "\"calibrationMessage\":\"%s\",", report);
    used +=
        snprintf(json + used, sizeof(json) - used,
                 "\"calibrationDraft\":{\"model\":\"%s\",\"points\":[", calibration_draft.model);
    for (unsigned i = 0; i < calibration_draft.count; i++)
        used += snprintf(json + used, sizeof(json) - used, "%s{\"position\":%.12g,\"region\":%u}",
                         i ? "," : "", calibration_draft.points[i].position,
                         calibration_draft.points[i].region);
    used += snprintf(json + used, sizeof(json) - used, "]},\"receiverStreamConnected\":%s,",
                     stream.stage == STREAM_SUBSCRIBED ? "true" : "false");
    used += snprintf(json + used, sizeof(json) - used,
                     "\"sampleSequence\":%u,\"sampleAgeMs\":%lld,\"holdingColors\":%s,"
                     "\"sourceFade\":%s,\"calibrating\":%s,",
                     sample_sequence, sample_at ? (long long)(now - sample_at) : -1,
                     continuity.holding ? "true" : "false",
                     (continuity.fading || bridge_phase == BRIDGE_FADING) ? "true" : "false",
                     CALIBRATION_ACTIVE ? "true" : "false");
    used += snprintf(json + used, sizeof(json) - used,
                     "\"returnValue\":true,\"piccapPort\":%u,\"connectionTest\":{\"state\":\"%s\","
                     "\"ledCount\":%u,\"elapsedMs\":%llu,\"error\":\"%s\"},\"lastOutputAgeMs\":%"
                     "lld,\"retryInMs\":%llu,\"captureState\":\"%s\",\"piccapError\":\"%s\",",
                     target.piccap_port, test_stage_name(), connection_test.leds,
                     (unsigned long long)connection_test.elapsed, connection_test.error,
                     last_output_at ? (long long)(now - last_output_at) : -1,
                     (unsigned long long)(retries.due > now ? retries.due - now : 0),
                     capture_source == SOURCE_PICCAP_STARTING   ? "Starting PicCap"
                     : capture_source == SOURCE_PICCAP_STOPPING ? "Stopping PicCap"
                     : piccap_owned                             ? "PicCap"
                     : capture                                  ? "Histogram"
                                                                : "Stopped",
                     piccap_error);
    used += snprintf(json + used, sizeof(json) - used,
                     "\"version\":\"" HYPERDRM_VERSION
                     "\",\"running\":%s,\"colorValid\":%s,\"frames\":%lu,\"captureRecoveries\":%u,"
                     "\"lastRecoveryMs\":%llu,\"endpoint\":\"%s\",\"host\":\"%s\",\"port\":%u,"
                     "\"receiver\":\"%s\",\"tokenSet\":%s,\"piccapTargetCompatible\":%s,"
                     "\"priority\":%d,\"uid\":%u,\"drmVerified\":false,\"mappingVerified\":false,"
                     "\"outputColor\":[%u,%u,%u],\"error\":\"%s\",\"regions\":[",
                     (capture || capture_source == SOURCE_PICCAP) ? "true" : "false",
                     color_valid ? "true" : "false", frames, capture_recoveries,
                     (unsigned long long)last_recovery_ms, endpoint, target.host, target.port,
                     target.receiver, target.token[0] ? "true" : "false",
                     target_piccap_compatible(&target) ? "true" : "false", PRIORITY,
                     (unsigned)geteuid(), sent_rgb[0], sent_rgb[1], sent_rgb[2], safe);
    for (int i = 0; i < 16; i++)
        used += snprintf(json + used, sizeof(json) - used, "%s[%u,%u,%u]", i ? "," : "", rgb[i][0],
                         rgb[i][1], rgb[i][2]);
    used += snprintf(json + used, sizeof(json) - used,
                     "],\"ledCount\":%u,\"spatialOutput\":%s,\"blackPercent\":[", led_count,
                     led_count ? "true" : "false");
    for (int i = 0; i < 16; i++)
        used += snprintf(json + used, sizeof(json) - used, "%s%u", i ? "," : "", black_percent[i]);
    snprintf(json + used, sizeof(json) - used,
             "],\"preset\":%u,\"saturationGain\":1.0,\"brightnessGain\":1.0,"
             "\"spatialInterpolation\":%s,\"blackBarDetection\":true,\"autostart\":%s,"
             "\"retryScheduled\":%s,\"retryPaused\":%s,\"retryBlocked\":%s,\"retryAttempts\":%u,"
             "\"piccapInstalled\":%s,\"piccapHandover\":%s,\"piccapConnected\":%s,\"activeSource\":"
             "\"%s\",\"playbackProtection\":\"%s\",\"letterboxDetected\":%s}",
             capture_preset, interpolation_enabled ? "true" : "false",
             boot_enabled() ? "true" : "false", retries.due ? "true" : "false",
             retries.paused ? "true" : "false", retries.blocked ? "true" : "false",
             retries.attempts, piccap_installed() ? "true" : "false",
             handover_enabled ? "true" : "false", piccap_connected ? "true" : "false",
             piccap_owned ? "PicCap" : "HyperDRM",
             protection == PLAYBACK_PROTECTED ? "protected"
             : protection == PLAYBACK_CLEAR   ? "non-secure decoder"
                                              : "unknown",
             capture && letterbox_mode ? "true" : "false");
    return respond(h, m, json);
}
