#define _DEFAULT_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <stdarg.h>
#include <time.h>
#include "version.h"
#include "histogram.h"
#include "blackbars.h"
#include "color_boost.h"
#include "interpolation.h"
#include "layout.h"
#include "autostart.h"
#include "retry.h"
#include "target.h"
#include "spatial_image.h"
#include "diagnostics.h"
#include "continuity.h"
#define SERVICE "org.webosbrew.histogramhyperion.service"
#define PRIORITY 149
typedef struct {
    int code;
    char *message;
    const char *file;
    int line;
    const char *func;
    void *padding;
    unsigned long magic;
} LSError;
typedef bool (*Callback)(void *, void *, void *);
typedef struct {
    const char *name;
    Callback function;
    int flags;
} Method;
static bool (*reply)(void *, void *, const char *, LSError *);
static const char *(*payload)(void *);
static bool (*error_init)(LSError *);
static void (*error_free)(LSError *);
static void *(*vt_init)(bool (*)(void));
static int (*vt_preset)(void *, int), (*vt_get)(void *, Histogram *), (*vt_finish)(void *);
static void *capture;
static char lg_diagnostic[256];
/* Process-local LG diagnostics; no changes to the loaded LG library. */
void printLog(const char *context, int level, const char *message_id, const char *file,
              const char *function, int line, const char *format, ...) {
    (void)context;
    (void)message_id;
    (void)file;
    if (capture && level > 3)
        return;
    if (format) {
        va_list args;
        va_start(args, format);
        vsnprintf(lg_diagnostic, sizeof(lg_diagnostic), format, args);
        va_end(args);
    }
    fprintf(stderr, "LG %s:%d: ", function ? function : "?", line);
    if (format) {
        va_list args;
        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);
    }
    fputc('\n', stderr);
}
static int output = -1;
static unsigned long frames;
static unsigned char rgb[16][3];
static unsigned char sent_rgb[3];
static unsigned black_percent[16];
static unsigned capture_preset;
static unsigned letterbox_mode, settling;
static int pending_letterbox = -1;
static uint64_t candidate_since, next_border_probe;
static bool color_valid;
static Continuity continuity;
static uint64_t sample_at, calibration_until;
static unsigned sample_sequence;
#define CALIBRATION_ACTIVE (calibration_until > milliseconds())
static unsigned led_count;
static LedBlend *led_blends;
static bool interpolation_enabled;
static RetryState retries;
static unsigned capture_recoveries;
static uint64_t recovery_started, last_recovery_ms, last_output_at;
static uint64_t milliseconds(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static char last_error[256];
static volatile sig_atomic_t revoked, quitting;
static int guard_pipe = -1;
static pid_t guard_pid = -1;
#define DEV_MODE "/var/luna/preferences/vt_devmode"
/* LG's developer gate is enabled only during initialization. A separate
 * guardian removes our own marker even if this process aborts or is killed.
 * An existing marker is preserved, as are files replaced by another owner. */
static bool enter_developer_mode(void) {
    int marker = open(DEV_MODE, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
    if (marker < 0)
        return errno == EEXIST;
    struct stat owned;
    if (fstat(marker, &owned) != 0) {
        close(marker);
        unlink(DEV_MODE);
        return false;
    }
    close(marker);
    int p[2];
    if (pipe(p) != 0) {
        unlink(DEV_MODE);
        return false;
    }
    pid_t child = fork();
    if (child < 0) {
        close(p[0]);
        close(p[1]);
        unlink(DEV_MODE);
        return false;
    }
    if (child == 0) {
        close(p[1]);
        struct pollfd fd = {p[0], POLLIN | POLLHUP, 0};
        /* Bounded cleanup also covers an indefinitely blocked initialization. */
        poll(&fd, 1, 10000);
        close(p[0]);
        struct stat current;
        int rc = 0;
        if (lstat(DEV_MODE, &current) == 0 && current.st_dev == owned.st_dev &&
            current.st_ino == owned.st_ino)
            rc = unlink(DEV_MODE);
        _exit(rc == 0 ? 0 : 1);
    }
    close(p[0]);
    guard_pipe = p[1];
    guard_pid = child;
    return true;
}
static bool leave_developer_mode(void) {
    if (guard_pipe < 0)
        return true;
    close(guard_pipe);
    guard_pipe = -1;
    int status;
    while (waitpid(guard_pid, &status, 0) < 0) {
        if (errno != EINTR)
            return false;
    }
    guard_pid = -1;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
static void quit_signal(int s) {
    (void)s;
    quitting = 1;
}
static bool policy(void) {
    revoked = 1;
    return true;
}
static void set_error(const char *s) {
    snprintf(last_error, sizeof(last_error), "%s", s);
    fprintf(stderr, "%s\n", s);
}
static int send_line(const char *line) {
    size_t left = strlen(line);
    const char *p = line;
    while (left) {
        ssize_t n = send(output, p, left, MSG_NOSIGNAL);
        if (n <= 0)
            return -1;
        p += n;
        left -= n;
    }
    return 0;
}
static bool receiver_ack(void) {
    char answer[4096];
    size_t used = 0;
    bool complete = false;
    while (used + 1 < sizeof(answer)) {
        ssize_t n = recv(output, answer + used, sizeof(answer) - used - 1, 0);
        if (n <= 0)
            break;
        used += (size_t)n;
        answer[used] = 0;
        if (strchr(answer, '\n')) {
            complete = true;
            break;
        }
    }
    PlaybackJson json = {0};
    if (!complete || !playback_json_init(&json))
        return false;
    void *root = json.parse(answer);
    bool ok = playback_true(&json, root, "success");
    if (root)
        json.put(root);
    dlclose(json.lib);
    return ok;
}
static bool read_layout(void) {
    const size_t capacity = 2 * 1024 * 1024;
    char *data = malloc(capacity + 1);
    if (!data)
        return false;
    bool complete = false;
    size_t used = 0;
    if (send_line("{\"command\":\"serverinfo\"}\n") == 0) {
        while (used < capacity) {
            ssize_t n = recv(output, data + used, capacity - used, 0);
            if (n <= 0)
                break;
            used += (size_t)n;
            data[used] = 0;
            char *end = strchr(data, '\n');
            if (end) {
                *end = 0;
                complete = true;
                break;
            }
        }
    }
    LedBlend *mapping = NULL;
    unsigned count = 0;
    bool ok = complete && layout_from_json(data, &mapping, &count);
    if (ok) {
        free(led_blends);
        led_blends = mapping;
        led_count = count;
    }
    free(data);
    interpolation_enabled = ok && calibration_effective() != CAL_AVERAGE;
    return ok;
}
static bool target_login(void) {
    if (!target.token[0])
        return true;
    char request[384], answer[4096];
    size_t used = 0;
    snprintf(request, sizeof(request),
             "{\"command\":\"authorize\",\"subcommand\":\"login\",\"token\":\"%s\"}\n",
             target.token);
    if (send_line(request) != 0)
        return false;
    while (used + 1 < sizeof(answer)) {
        ssize_t n = recv(output, answer + used, sizeof(answer) - used - 1, 0);
        if (n <= 0)
            return false;
        used += (size_t)n;
        answer[used] = 0;
        if (strchr(answer, '\n'))
            break;
    }
    PlaybackJson json = {0};
    if (!playback_json_init(&json))
        return false;
    void *root = json.parse(answer);
    bool ok = playback_true(&json, root, "success");
    if (root)
        json.put(root);
    dlclose(json.lib);
    return ok;
}
static void layout_refresh_reset(uint64_t due);
static void release_capture(bool clear_output) {
    layout_refresh_reset(milliseconds() + 5000);
    free(led_blends);
    led_blends = NULL;
    led_count = 0;
    interpolation_enabled = false;
    color_valid = false;
    memset(rgb, 0, sizeof(rgb));
    memset(sent_rgb, 0, sizeof(sent_rgb));
    if (output >= 0) {
        if (clear_output && frames) {
            char line[80];
            snprintf(line, sizeof(line), "{\"command\":\"clear\",\"priority\":%d}\n", PRIORITY);
            send_line(line);
        }
        close(output);
        output = -1;
    }
    if (capture) {
        vt_finish(capture);
        capture = NULL;
    }
}
static void stop_capture(void) {
    release_capture(true);
}
static bool respond(void *h, void *m, const char *s) {
    if (!h || !m)
        return true;
    LSError e;
    error_init(&e);
    bool ok = reply(h, m, s, &e);
    error_free(&e);
    return ok;
}
#include "handover.h"
#include "layout_refresh.h"
#define RECEIVER_LAYOUT_REVISION layout_updates
#include "receiver_stream.h"
#include "source_bridge.h"
#include "service_api.h"
#include "capture_loop.h"
static void *symbol(void *lib, const char *name) {
    void *s = dlsym(lib, name);
    if (!s) {
        fprintf(stderr, "Missing symbol %s\n", name);
        exit(2);
    }
    return s;
}
int main(void) {
    signal(SIGTERM, quit_signal);
    signal(SIGINT, quit_signal);
    if (boot_enabled() && !set_boot(true))
        fprintf(stderr, "Own legacy boot hook migration failed\n");
    void *vt = dlopen("libvtcapture.so.1", RTLD_NOW),
         *ls = dlopen("libluna-service2.so.3", RTLD_NOW),
         *glib = dlopen("libglib-2.0.so.0", RTLD_NOW);
    if (!vt || !ls || !glib) {
        fprintf(stderr, "Missing LG library: %s\n", dlerror());
        return 2;
    }
    vt_init = symbol(vt, "vtCapture_initHistogram");
    vt_preset = symbol(vt, "vtCapture_setPresetHistogram");
    vt_get = symbol(vt, "vtCapture_getHistogram");
    vt_finish = symbol(vt, "vtCapture_finalizeHistogram");
    call_once = symbol(ls, "LSCallOneReply");
    cancel_call = symbol(ls, "LSCallCancel");
    if (playback_json_init(&playback_json)) {
        handover_load();
        target_load(&playback_json, TARGET_PREF, &target);
        calibration_load(&playback_json, CALIBRATION_PREF);
    }
    reply = symbol(ls, "LSMessageReply");
    payload = symbol(ls, "LSMessageGetPayload");
    error_init = symbol(ls, "LSErrorInit");
    error_free = symbol(ls, "LSErrorFree");
    bool (*reg)(const char *, void **, LSError *) = symbol(ls, "LSRegister");
    bool (*category)(void *, const char *, Method *, void *, void *, LSError *) =
        symbol(ls, "LSRegisterCategory");
    bool (*attach)(void *, void *, LSError *) = symbol(ls, "LSGmainAttach");
    bool (*unreg)(void *, LSError *) = symbol(ls, "LSUnregister");
    void *(*loop_new)(void *, int) = symbol(glib, "g_main_loop_new");
    int (*iterate)(void *, int) = symbol(glib, "g_main_context_iteration");
    unsigned (*timer)(unsigned, int (*)(void *), void *) = symbol(glib, "g_timeout_add");
    void (*loop_unref)(void *) = symbol(glib, "g_main_loop_unref");
    void *handle = NULL, *loop = loop_new(NULL, 0);
    LSError e;
    error_init(&e);
    Method methods[] = {{"start", start, 0},
                        {"stop", stop, 0},
                        {"status", status, 0},
                        {"autostart", autostart, 0},
                        {"restartService", restart_service, 0},
                        {"handover", handover, 0},
                        {"settings", settings, 0},
                        {"testConnection", test_connection, 0},
                        {"calibration", set_calibration, 0},
                        {"calibrationSession", calibration_session, 0},
                        {"calibrationDraft", draft_calibration, 0},
                        {"calibrationResult", report_calibration, 0},
                        {NULL, NULL, 0}};
    if (!reg(SERVICE, &handle, &e) || !category(handle, "/", methods, NULL, NULL, &e) ||
        !attach(handle, loop, &e)) {
        fprintf(stderr, "Luna registration failed: %s\n", e.message ? e.message : "unknown");
        error_free(&e);
        return 3;
    }
    service_handle = handle;
    if (!boot_enabled())
        retry_pause(&retries);
    timer(100, tick, NULL);
    while (!quitting) {
        iterate(NULL, 0);
        usleep(10000);
    }
    stream_close(milliseconds());
    test_close();
    stop_capture();
    if (piccap_owned) {
        handover_enabled = false;
        retry_pause(&retries);
        uint64_t deadline = milliseconds() + 4000;
        while (piccap_owned && milliseconds() < deadline) {
            handover_tick();
            iterate(NULL, 0);
            usleep(10000);
        }
    }
    unreg(handle, &e);
    error_free(&e);
    loop_unref(loop);
    return 0;
}
