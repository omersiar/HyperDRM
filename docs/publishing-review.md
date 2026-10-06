# webOSbrew publishing review

Requirements: https://www.webosbrew.org/develop/guides/publishing/rules/
Reviewed: 2026-10-06. No submission has been opened.

## Human review and disclosure

The implementation and original icon were produced with substantial AI assistance. The owner directed development and tested visual behavior on a real LG G4 with webOS 24. These user tests do not establish that a human has reviewed the entire source. Before submission, a maintainer must meaningfully review the implementation and be able to explain its design; the submission's AI disclosure must reflect the actual history. Removing creation notes does not change that history. The generated icon should be reviewed or replaced by the owner, according to the recommendation that icons and descriptions be the author's own work.

Design review points: histogram bin width and RGB averages; LG developer-marker lifetime; bounded Hyperion layout parsing; interpolation/calibration limits; black-bar preset transitions; root-only methods; boot-hook migration; source-transition retries and cancellation.

## DRM and content

No media, DRM keys, ROMs, account bypasses or third-party capture binaries are distributed. The calibration video is a locally generated coordinate ramp. The app sends RGB color values rather than video frames.

The capture initializer temporarily creates `/var/luna/preferences/vt_devmode` to pass LG's developer gate. Although no decryption or DRM patch is implemented, it has not been established whether the resulting access to color data during protected playback is acceptable under webOSbrew's prohibition of DRM circumvention. This must be reviewed explicitly before submission. Do not certify DRM compliance solely because only colors are output.

## Hardware and removal

No writes to signed system partitions. Writes outside the service container are limited to the own Homebrew autostart symlink, bounded boot log and transient developer marker. A guardian removes the own marker after initialization or process termination; a previously existing marker is preserved. Concurrent writes/power loss remain review considerations.

The boot hook is a symlink to `service/boot.sh` in the installed container. The script checks for its executable and service metadata before acting, checks them again on every retry, and exits when the hook is disabled or the target is removed. Since 0.6.4 it delegates ongoing Hyperion recovery to the native service once startup or background retries are accepted. Only an exact known legacy own hook is migrated; foreign files/symlinks are rejected. Logs are truncated at each invocation and contain only startup/delegation/cancellation messages, so waiting has no unbounded boot-log growth.

## Source, licence and release

Public repository page and raw README access were verified without credentials (HTTP 200); the API restricts anonymous requests. An identifiable project licence must be chosen by the owner. No vendored proprietary libraries are shipped. A release manifest must record the exact IPK hash/size and root requirement. The apps-repo package must use pool `main`, a public sourceUrl and public HTTPS manifest/IPK/icon URLs. No listing is ready until those URLs resolve without credentials.

## Evidence and remaining checks

G4/webOS 24: original capture, spatial output, color boost, dynamic layout tests (50/500), black-bar classification and boot-hook invocation tested. Other models are unsupported until tested. Autostart symlink enable/disable, exact legacy migration, rejection of foreign files/symlinks, temporary-file collision and missing-target removal passed in an isolated ARM test on the TV. Version 0.6.1 was installed and started on the G4; the existing copied production hook migrated to the correct container symlink. Boot script syntax passed on the TV. Actual cold boot remains untested. A real uninstall and clean-state reinstall were performed on 2026-10-06; details follow below. Public listing eligibility remains pending human and DRM review, release URL validation and licence choice. The runtime reports Rockhopper 10.2.2-57 and device SoC o24; these measured values are used for the package requirements rather than an unverified minimum release.

## Actual uninstall/reinstall test (2026-10-06)

Platform: LG G4, Rockhopper 10.2.2-57, SoC o24. Package: HyperDRM 0.6.1.

- Removed the running app through `com.webos.appInstallService/dev/remove`; installer reported `removed`, progress 100. Both app and service containers disappeared and the native process stopped.
- The own startup symlink became dangling. Homebrew run-parts dry-run reported the missing target and did not list it among executable hooks. This produces a diagnostic line, rather than executing an orphaned script.
- A saved copy of the own script, invoked with its installed target absent, exited 0 immediately. The boot log hash was unchanged.
- Removed only the verified own dangling symlink for a fresh-state test. Reinstallation reported `update:false`. Before root elevation, status showed running:false, autostart:false and a non-root UID.
- Root elevation after that first status call did not replace the already-running unprivileged service process. Start failed explicitly rather than creating the developer marker. Verified the exact own process path and terminated that process with SIGTERM; the next activation started as root and color output succeeded. Installation instructions now require elevation before first activation and explain this recovery case.
- Restored the user's enabled autostart setting. No Hyperion configuration, PicCap or foreign hook was changed.

This is an actual package lifecycle test, not a TV factory reset and not a cold boot.

## Persistent startup/reconnect retries (0.6.4)

With autostart enabled, the native service retries startup every five seconds without an attempt limit. Hyperion TCP availability and layout are checked before acquiring capture. The boot hook exits once native startup or background retries are accepted; it no longer owns the long-running reconnect loop. Manual Stop pauses retries for the current service session. Start or re-enabling autostart resumes them. Failed developer-marker cleanup blocks retries until manual intervention. Turning autostart off cancels automatic pending attempts.

ARM retry-state tests on the G4 passed 10000 delayed-start attempts, five-second pacing, manual Stop, disabling autostart, the cleanup safety block, bounded non-autostart source recovery and disconnect retry. Live tests temporarily stopped and restored Hyperion without changing configuration; automatic retry continued beyond six attempts (observed counts 10 and 11). Manual Stop left retryPaused:true and retryScheduled:false after Hyperion was restored.

A second live disconnect test confirmed retryScheduled:true while Hyperion was unavailable, followed by running:true and colorValid:true automatically within the six-second observation window after Hyperion restarted, without a new HyperDRM Start command.

## Optional PicCap handover (0.7.0)

The default-off Auto PicCap switch stores its preference in the own service container. If enabled, it uses the installed PicCap service's existing start/stop/status methods; it does not rewrite PicCap settings or its autostart hook. Only foreground media decoder resources without `SVP_CPB` are eligible, after two samples. Any secure resource or unknown/stale state restores the histogram path. VT capture is released and HyperDRM priority cleared before starting PicCap; stopped status is required before histogram capture resumes. Manual Stop pauses both managed paths. Shutdown attempts to stop a managed PicCap session before unregistering; a forced kill or unresponsive external service cannot guarantee this cleanup.

On the G4, playback-classification and isolated controller tests passed: secure/clear/unknown states, mixed pipelines, malformed records, two-sample gating, exclusive transitions, manual Stop, disabling handover and timeout cancellation. The fifth control passed remote-navigation regression tests. Version 0.7.0 was installed and elevated; with Disney+ playing, enabling handover reported protected playback, maintained histogram color output and left PicCap stopped. The test preference was restored to off. Live handover was then verified with the user's unprotected test video: PicCap reported videoRunning:true and connected:true, and Hyperion selected its visible FlatBuffer input at priority 150 with spatial output for the current 120-LED layout. Disabling handover stopped PicCap and restored histogram output. Re-enabling handover and switching back to DRM automatically stopped PicCap and restored HyperDRM as Hyperion's visible priority-149 source. The preference was restored to off after the complete test.

## Capture blackout diagnosis and recovery (0.7.1)

A live 0.7.0 observation reproduced the reported blackout while Auto PicCap was disabled and PicCap was stopped. LG revoked histogram capture; the controller cleared priority 149 and waited five seconds before reinitializing. Hyperion LED-stream measurements recorded an all-black interval of approximately 4.9 seconds. WLED was reachable and reported an active DDP stream from the TV; no WLED settings were changed.

Version 0.7.1 preserves the last update's existing 1.5-second expiry during a revocation recovery, instead of explicitly clearing it. Initialization retries begin after 200 ms with two additional 400 ms retry intervals before returning to normal five-second pacing. This does not extend stale colors indefinitely. Explicit capture stops and PicCap handovers retain their clear behavior. Status records recovery count and elapsed initialization time.

ARM retry and handover regression tests passed on the G4, as did remote navigation tests. Version 0.7.1 was installed and elevated on the TV. A later actual LG revocation recovered in 762 ms, including one refused initialization attempt before success. A 55-second Hyperion observation contained 125 color samples with no all-black output. These measurements do not guarantee that prolonged LG capture unavailability or actual black video scenes cannot darken the LEDs.

## Configurable Hyperion/HyperHDR receiver (0.8.0)

The app can save receiver type, numeric IPv4/IPv6 address, JSON TCP port and an optional API token. Targets and tokens are validated before replacing the configuration or interrupting capture. The own container stores `target.json` atomically with mode 0600; status omits the token value. TCP connection attempts are bounded to one second. Authentication and remote layout validation happen before acquiring LG capture. The receiver's own authorization and LED-device settings are not changed. Auto PicCap remains limited to the default local target; custom targets use histogram capture rather than sending PicCap data to an independently configured receiver.

Native target tests passed on the G4, including invalid values, token retention/removal, private persistence and TCP success/refusal. Handover and input-field remote-navigation regression tests passed. HyperDRM 0.8.0 was installed and tested against the user's HyperHDR 22.0.0 on localhost:19444: acknowledged spatial color updates for its 120-LED layout. A temporary test relay on another LAN host at port 19445 exercised synthetic token login and fragmented replies, forwarding actual layout/color/clear commands to HyperHDR. Omitting the token preserved it; an invalid target was rejected without changing the active target. A verified own-service process restart loaded the saved HyperHDR target. The test relay was stopped and the target restored to localhost:19444 with no saved token. Network tests used IPv4; IPv6 parsing is covered by unit tests, but an IPv6 receiver was not available for an end-to-end test. Cold boot remains untested.

The API compatibility was checked against official HyperHDR `sources/api/HyperAPI.cpp` and `sources/base/HyperHdrInstance.cpp`: serverinfo includes the LED layout, color accepts RGB arrays, and token authorization precedes API commands.
