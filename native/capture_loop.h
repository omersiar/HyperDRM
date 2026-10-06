/* Capture scheduler and histogram processing. */
static int tick(void *unused) {
    (void)unused;
    uint64_t now = milliseconds();
    model_tick(now);
    test_tick(&playback_json, now);
    layout_refresh_tick(&playback_json, now, capture || capture_source == SOURCE_PICCAP);
    stream_tick(&playback_json, now, handover_enabled && !retries.paused && !retries.blocked);
    bool owned = handover_tick();
    bridge_tick(now);
    if (owned)
        return 1;
    if (!capture) {
        retry_update(&retries, boot_enabled() && geteuid() == 0, false, milliseconds());
        if (retry_take(&retries, milliseconds())) {
            start(NULL, NULL, NULL);
            if (!capture)
                retry_failure(&retries, milliseconds());
        }
        return 1;
    }
    if (revoked) {
        set_error("LG revoked capture; recovering after source transition");
        capture_recoveries++;
        recovery_started = milliseconds();
        release_capture(false);
        retry_source_transition(&retries, milliseconds());
        return 1;
    }
    if (settling) {
        settling--;
        return 1;
    }
    if (!CALIBRATION_ACTIVE && letterbox_mode && capture_preset == 1 &&
        milliseconds() >= next_border_probe) {
        if (vt_preset(capture, 0) != 0) {
            set_error("Border probe preset failed");
            stop_capture();
            return 1;
        }
        capture_preset = 0;
        settling = 2;
        return 1;
    }
    Histogram hist[16] = {0};
    int rc = vt_get(capture, hist);
    bool populated = !rc && histogram_populated(hist);
    if (!populated) {
        unsigned char held[16][3];
        color_valid = false;
        if (!continuity_frame(&continuity, false, rgb, now, held)) {
            set_error("Histogram data unavailable beyond hold window; recovering capture");
            release_capture(false);
            retry_source_transition(&retries, now);
            return 1;
        }
        char *line = region_command(held, led_blends, led_count,
                                    (unsigned)(1500 - (now - continuity.missing_at)));
        if (!line || send_line(line)) {
            free(line);
            set_error("Receiver connection lost while holding colors");
            stop_capture();
            return 1;
        }
        free(line);
        if (!receiver_ack()) {
            set_error("Receiver did not acknowledge held colors");
            stop_capture();
            return 1;
        }
        last_output_at = now;
        return 1;
    }
    sample_at = now;
    sample_sequence++;
    unsigned aggregate[3] = {0};
    for (int i = 0; i < 16; i++) {
        black_percent[i] = 100;
        for (int c = 0; c < 3; c++) {
            uint64_t total = 0, black = 0;
            for (int b = 0; b < 256; b++) {
                total += hist[i].bins[c][b];
                if (b <= 12)
                    black += hist[i].bins[c][b];
            }
            unsigned fraction = total ? (unsigned)(100 * black / total) : 0;
            if (fraction < black_percent[i])
                black_percent[i] = fraction;
        }
    }
    for (int i = 0; i < 16; i++)
        for (int c = 0; c < 3; c++) {
            rgb[i][c] = histogram_mean(hist[i].bins[c]);
            aggregate[c] += rgb[i][c];
        }
    if (!CALIBRATION_ACTIVE && capture_preset == 0) {
        int candidate = letterbox_candidate(black_percent, rgb);
        uint64_t now = milliseconds();
        if (candidate < 0)
            pending_letterbox = -1;
        else if (candidate != pending_letterbox) {
            pending_letterbox = candidate;
            candidate_since = now;
        } else if ((unsigned)candidate != letterbox_mode && now - candidate_since >= 2000)
            letterbox_mode = (unsigned)candidate;
        if (letterbox_mode) {
            if (vt_preset(capture, 1) != 0) {
                set_error("Letterbox preset failed");
                stop_capture();
                return 1;
            }
            capture_preset = 1;
            settling = 2;
            next_border_probe = now + 2000;
            return 1;
        }
    }
    for (int c = 0; c < 3; c++)
        sent_rgb[c] = (unsigned char)(aggregate[c] / 16);
    unsigned char boosted_mean[3];
    boost_color(sent_rgb, boosted_mean);
    memcpy(sent_rgb, boosted_mean, 3);
    unsigned char mapped_rgb[16][3], stable_rgb[16][3];
    calibration_colors(rgb, mapped_rgb);
    continuity_frame(&continuity, true, mapped_rgb, now, stable_rgb);
    interpolation_enabled = led_count && calibration_effective() != CAL_AVERAGE;
    char *line = region_command(stable_rgb, led_blends, led_count, 1500);
    if (!line) {
        set_error("Color buffer allocation failed");
        stop_capture();
        return 1;
    }
    int failed = send_line(line);
    free(line);
    if (failed) {
        set_error("Receiver connection lost");
        stop_capture();
        return 1;
    }
    if (!receiver_ack()) {
        set_error("Receiver did not acknowledge color data");
        stop_capture();
        return 1;
    }
    last_error[0] = 0;
    color_valid = true;
    last_output_at = milliseconds();
    frames++;
    return 1;
}
