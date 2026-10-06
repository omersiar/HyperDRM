# HyperDRM

Ambient Light for webOS, Hyperion and HyperHDR. An own webOS app and native ARM service that reads LG color histograms and sends spatial LED colors to a configurable Hyperion or HyperHDR receiver.

![HyperDRM logo](app/icon.png)

## Features

- Spatial colors from 16 LG histogram regions with interpolated transitions along the screen perimeter.
- LED count, ordering and screen rectangles read from Hyperion at each service start. No fixed 200-LED layout; layouts with 50 and 500 LEDs were tested.
- Automatic detection of top/bottom black bars on the calibrated LG G4.
- Original saturation and brightness (both gains 1.0).
- Start, stop, status and persistent boot autostart switch in the TV app.
- Configurable IPv4/IPv6 receiver and JSON TCP port, optional token authentication, priority 149, up to 10 updates per second.

## Requirements and limits

Tested on a rooted LG G4 with webOS 24, Homebrew Channel and Hyperion 2.0.16 and HyperHDR 22.0.0 tested on the TV. The native service requires root elevation and the TV's existing `libvtcapture`, Luna, GLib and JSON-C libraries. Other TV models or firmware versions have not been validated.

DRM playback color output was confirmed on the test TV. Support depends on LG's capture implementation; this does not guarantee support on other devices or apps. HyperDRM transmits colors, not captured video frames. The video preview does not show the TV picture. HyperHDR may show a generated histogram color plane instead.

The 16 histogram samples are independent measurements; interpolation produces smoother transitions without adding independently captured detail. Sample positions are approximate G4 calibration values. Top/bottom bar detection is supported; side bars, arbitrary bar thickness and HDR color calibration remain limited.

Only the own service is modified. Optional Auto PicCap can configure its receiver endpoint and start and stop the installed PicCap service through its existing API. No Hue, LG library, driver or firmware binaries are included. For histogram initialization, the service temporarily creates LG's developer marker and uses a guardian process to remove its own marker afterward; an existing marker is preserved.

## Installation

The current package is [HyperDRM_0.9.0_arm.ipk](HyperDRM_0.9.0_arm.ipk). Install it using the TV's webOS/Homebrew package installation workflow.

Elevate the installed own service through Homebrew Channel:

```sh
luna-send -n 1 luna://org.webosbrew.hbchannel.service/elevateService '{"id":"org.webosbrew.histogramhyperion.service"}'
```

Perform elevation before first opening the app or calling its service. If the service was already started without root, elevation updates its launch permissions but does not restart the existing process: terminate only the own `histogram-service` process (verify its executable path first), then reopen the app or call start again. Closing the UI alone does not terminate the native service.

Open **HyperDRM** on the TV and select **Start**. Use the remote arrow buttons to move between controls and OK to activate the selected control. Magic Remote pointer input is also supported. The receiver's JSON TCP server must be enabled at the IP/port saved in Receiver settings (default 127.0.0.1:19444). The existing Hyperion LED device/output configuration remains responsible for controlling the hardware.

The internal app/service IDs remain `org.webosbrew.histogramhyperion` and `org.webosbrew.histogramhyperion.service` so existing installations and the autostart hook remain compatible. Hyperion displays the source as **HyperDRM**. After changing the LED layout, restart the service.

## Autostart

The app's **Autostart: On/Off** switch controls the own hook `/var/lib/webosbrew/init.d/60-histogramhyperion`. Homebrew Channel runs it during boot after service elevation. The boot hook waits until the native service accepts startup, then exits. With autostart enabled, the native service retries every five seconds without an attempt limit when startup fails or the Hyperion connection drops. It checks Hyperion before acquiring the LG capture resource. The Homebrew failsafe can suppress boot hooks.

**Stop** ends the current capture session and pauses retries until **Start** or the next service launch/boot; it leaves the saved autostart preference unchanged. The hook is a symlink to `boot.sh` inside the service container. Removal leaves a harmless dangling link; the script also exits when its service target is absent. Turning autostart off removes the own hook and cancels waiting boot attempts. A foreign file or symlink at that path will not be overwritten.

Boot log: `/var/lib/webosbrew/histogramhyperion-boot.log`.

## Build

Use an ARM EABI cross-compiler and Python 3:

```sh
arm-linux-gnueabi-gcc -Wall -Wextra -Werror -O2 -rdynamic native/service.c -ldl -o service/histogram-service
python3 build_package.py
python3 build_manifest.py
```

The package builder bundles only the own app, native executable, metadata and logo assets. A built executable and current IPK are included for the tested platform.

## Tests and calibration

The native tests cover histogram decoding, black bar classification, color boost, interpolation and dynamic layout validation. Build each `native/test_*.c` for ARM and run on the TV. The layout test needs the TV's JSON-C library and linking with `-ldl`.

Example:

```sh
arm-linux-gnueabi-gcc -Wall -Wextra -Werror -O2 native/test_layout.c -ldl -o test-layout
arm-linux-gnueabi-gcc -Wall -Wextra -Werror -O2 native/test_interpolation.c -o test-interpolation
```

The tests passed on the G4. Boot-hook invocation started a stopped service; an actual cold boot has not yet been tested. A real uninstall/reinstall cycle was tested on the G4: containers and running process were removed, the dangling startup link did not execute, and fresh installation plus root elevation and service restart restored color output. Isolated ARM tests also cover autostart deactivation, legacy-hook migration, foreign-hook protection and removal of the target container.

`calibration-gradient.mp4` is the own red/green coordinate ramp used for calibration. `spatial-calibration.json` and `spatial-interpolation.json` record the calibration and example layout. The offline mapping scripts require locally obtained `calibration-samples.jsonl` and `serverinfo.json`; these diagnostic snapshots are not distributed. Runtime mapping is implemented in `native/layout.h` and reads the actual Hyperion layout.

To regenerate the test video, install `imageio-ffmpeg` in your Python environment and run `python3 make_calibration.py`.

## Logo

The logo master is `app/logo.png`, with 80 px and 130 px installation icons.

## Publishing status

See [the publishing review](docs/publishing-review.md) for the requirements audit and unresolved submission conditions. This repository is not a claim that webOSbrew has approved the app.

Remote navigation regression tests: `node tests/remote-navigation.test.js`.

## Optional automatic PicCap handover

Version 0.7.0 adds **Auto PicCap**, disabled by default. The switch is available when PicCap is installed. Keep PicCap's own autostart disabled. From 0.9.0, HyperDRM manages PicCap's receiver address, FlatBuffer port and Unix socket setting when automatic handover is enabled; other capture preferences and the autostart preference are preserved.

When the session is started, the controller polls LG's active media resources. Two consecutive foreground media-decoder samples without a Secure Video Path resource allow PicCap to start. HyperDRM releases its histogram capture and clears priority 149 first, allowing PicCap's configured input priority to take over. Any `SVP_CPB` resource selects protected playback; missing, stale, malformed, idle or HDMI state selects the conservative HyperDRM fallback. PicCap must confirm that it has stopped before histogram capture resumes. Detection is specific to the tested G4 resource model, not a universal DRM-state API.

The switch is saved in the own service container and applies after reboot when Autostart is enabled. Stop pauses both managed capture paths until Start. Turning Auto PicCap off stops a PicCap session started by this controller and restores HyperDRM. Avoid starting another capture service independently while automatic handover is active.

The app displays the selected source and PicCap's Hyperion connection status. Histogram region swatches are hidden while PicCap supplies the colors. Playback classification and isolated controller tests are in `native/test_playback.c` and `native/test_handover.c`; link both with `-ldl`.

## Capture interruption recovery

LG may revoke histogram capture during playback transitions. From 0.7.1, HyperDRM releases capture while leaving its last color update to expire normally after 1.5 seconds, then retries initialization after 200 ms and up to two additional 400 ms intervals. Successful recovery clears this rapid retry budget. Persistent failure returns to the existing five-second retry interval; manual Stop, PicCap handover and shutdown still explicitly clear the own color source. Status exposes `captureRecoveries` and `lastRecoveryMs` for diagnosis.

## Hyperion and HyperHDR receiver settings

Choose **Receiver: Hyperion/HyperHDR**, enter an IPv4 or IPv6 address and the JSON TCP port, then select **Save receiver**. Hyperion uses per-LED JSON color arrays; HyperHDR uses a synthetic spatial RGB image because HyperHDR 22 treats the color command as a solid color. Both use clear and serverinfo. Use the JSON port (usually 19444), not the FlatBuffer port (usually 19400) or web interface port. Numeric IP addresses are supported; hostnames and scoped IPv6 addresses are not currently accepted. Layout and LED count come from the selected receiver, including remote receivers.

Settings are saved in `target.json` inside the own service container and loaded after service restart/boot. Saving valid settings switches a running session to the new receiver and clears the own priority at the old receiver. Invalid settings leave the current target intact. Saving while manually stopped does not start a session. Uninstalling removes the saved settings with the container.

For a receiver requiring authentication, enter its API token. The token file is mode 0600; status only reports whether a token is saved. Leaving the input blank retains it; **Remove saved token** deletes it when saving. A token login happens before requesting the layout or initializing LG capture. JSON TCP traffic is unencrypted, so use a trusted network. Receiver authorization rules are unchanged.

Auto PicCap uses the selected receiver IP and the separate **PicCap FlatBuffer port** (default 19400), including remote IPv4 receivers and custom ports. Enable the receiver FlatBuffer server. The tested PicCap client supports IPv4 only: local ::1 is mapped to 127.0.0.1 for PicCap, while remote IPv6 keeps histogram capture. API tokens authenticate JSON only; PicCap uses the receiver FlatBuffer protocol.

`native/test_target.c` verifies validation, token retention/removal, private persistence and TCP connection/refusal. `tests/receiver-proxy.py` is a test-only relay with synthetic token login and fragmented replies. Local HyperHDR 22.0.0 and a remote relay using a different IP/port were tested on the G4; the relay forwarded real color updates to HyperHDR without changing its configuration.

## HyperHDR spatial output (0.8.1)

HyperHDR 22's JSON `color` handler ultimately registers only the first RGB triplet as a solid color. Version 0.8.1 uses JSON `image` output for the HyperHDR receiver selection. A cached 160x90 geometry interpolates the calibrated histogram samples into a synthetic color plane; HyperHDR applies its configured LED rectangles to that plane. The payload contains generated colors, not captured or decrypted video frames. HyperHDR's image processing and color calibration affect the resulting LED colors. Select a spatial image mapping mode in HyperHDR; a global single-color mapping mode will still give uniform LEDs.

Hyperion retains its original per-LED color-array output. Receiver IP, JSON port, optional token, priority, timeout and reconnection behavior remain shared. Tests cover exact RGB base64 encoding, image command metadata and spatial edge variation. Output must be verified through the receiver's LED stream, not solely a successful command acknowledgment.

## Diagnostics and connection test (0.9.0)

Save the receiver settings, then select **Test connection**. The test opens a separate nonblocking JSON connection, logs in with the saved token when present, and requests and validates the LED layout. It sends no colors, clears no priorities and acquires no TV capture. Results include success/failure, validated LED count, elapsed time and an actionable error. A successful test verifies connectivity and layout access, not the physical LEDs or FlatBuffer input.

The diagnostic line reports service version/root status, capture transition, playback classification, capture recovery count/time, age of the last histogram output and scheduled retry timing. PicCap reports its own connection status; histogram output age is not PicCap frame age. Color calibration, saturation, brightness and smoothing belong in Hyperion/HyperHDR; HyperDRM keeps neutral gains.

Before PicCap starts, its endpoint is read through getSettings. Matching settings are reused. Otherwise setSettings updates only address, port and unix-socket:false; the official implementation restarts PicCap itself, so the controller checks status rather than issuing another start. Endpoint changes are saved by PicCap and remain after disabling handover. Changing the receiver stops the managed PicCap session before configuring the new endpoint. Settings errors/timeouts stop PicCap before histogram fallback, with a cooldown to avoid rapid restarts.

Feature tests are native/test_diagnostics.c, native/test_target.c, native/test_handover.c and tests/receiver-ui.test.js. The native tests cover fragmented replies, rejected authentication, invalid layout, refused connection, timeout, custom PicCap endpoint, configuration failure and exclusive capture ownership. Test-only JSON and FlatBuffer relays allow a real receiver to be reached through another IP and ports without changing receiver configuration.
