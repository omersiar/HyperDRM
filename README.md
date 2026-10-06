# HyperDRM

Ambient Light for webOS and Hyperion. An own webOS app and native ARM service that reads LG color histograms and sends spatial LED colors to a local Hyperion instance.

![HyperDRM logo](app/icon.png)

## Features

- Spatial colors from 16 LG histogram regions with interpolated transitions along the screen perimeter.
- LED count, ordering and screen rectangles read from Hyperion at each service start. No fixed 200-LED layout; layouts with 50 and 500 LEDs were tested.
- Automatic detection of top/bottom black bars on the calibrated LG G4.
- Saturation and brightness boost (2.0 and 1.33 respectively).
- Start, stop, status and persistent boot autostart switch in the TV app.
- Local Hyperion JSON connection on `127.0.0.1:19444`, priority 149, up to 10 updates per second.

## Requirements and limits

Tested on a rooted LG G4 with webOS 24, Homebrew Channel and Hyperion 2.0.16 installed on the TV. The native service requires root elevation and the TV's existing `libvtcapture`, Luna, GLib and JSON-C libraries. Other TV models or firmware versions have not been validated.

DRM playback color output was confirmed on the test TV. Support depends on LG's capture implementation; this does not guarantee support on other devices or apps. HyperDRM transmits colors, not captured video frames. The video preview in Hyperion therefore does not show the TV picture.

The 16 histogram samples are independent measurements; interpolation produces smoother transitions without adding independently captured detail. Sample positions are approximate G4 calibration values. Top/bottom bar detection is supported; side bars, arbitrary bar thickness and HDR color calibration remain limited.

Only the own service is modified. No Hue, LG library, driver or firmware binaries are included. For histogram initialization, the service temporarily creates LG's developer marker and uses a guardian process to remove its own marker afterward; an existing marker is preserved.

## Installation

The current package is [HyperDRM_0.6.2_arm.ipk](HyperDRM_0.6.2_arm.ipk). Install it using the TV's webOS/Homebrew package installation workflow.

Elevate the installed own service through Homebrew Channel:

```sh
luna-send -n 1 luna://org.webosbrew.hbchannel.service/elevateService '{"id":"org.webosbrew.histogramhyperion.service"}'
```

Perform elevation before first opening the app or calling its service. If the service was already started without root, elevation updates its launch permissions but does not restart the existing process: terminate only the own `histogram-service` process (verify its executable path first), then reopen the app or call start again. Closing the UI alone does not terminate the native service.

Open **HyperDRM** on the TV and select **Start**. Hyperion's JSON server must be enabled on port 19444. The existing Hyperion LED device/output configuration remains responsible for controlling the hardware.

The internal app/service IDs remain `org.webosbrew.histogramhyperion` and `org.webosbrew.histogramhyperion.service` so existing installations and the autostart hook remain compatible. Hyperion displays the source as **HyperDRM**. After changing the LED layout, restart the service.

## Autostart

The app's **Autostart: On/Off** switch controls the own hook `/var/lib/webosbrew/init.d/60-histogramhyperion`. Homebrew Channel runs it during boot after service elevation. It retries start while the TV and Hyperion initialize, with up to 120 attempts separated by five seconds. The Homebrew failsafe can suppress boot hooks.

**Stop** ends the current capture session and leaves the saved autostart preference unchanged. The hook is a symlink to `boot.sh` inside the service container. Removal leaves a harmless dangling link; the script also exits when its service target is absent. Turning autostart off removes the own hook and cancels waiting boot attempts. A foreign file or symlink at that path will not be overwritten.

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
