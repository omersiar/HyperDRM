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

The boot hook is a symlink to `service/boot.sh` in the installed container. The script checks for its executable and service metadata before acting, checks them again on every retry, and exits when the hook is disabled or the target is removed. Only an exact known legacy own hook is migrated; foreign files/symlinks are rejected. Logs are truncated at each invocation and per-attempt output is bounded.

## Source, licence and release

Public repository page and raw README access were verified without credentials (HTTP 200); the API restricts anonymous requests. An identifiable project licence must be chosen by the owner. No vendored proprietary libraries are shipped. A release manifest must record the exact IPK hash/size and root requirement. The apps-repo package must use pool `main`, a public sourceUrl and public HTTPS manifest/IPK/icon URLs. No listing is ready until those URLs resolve without credentials.

## Evidence and remaining checks

G4/webOS 24: original capture, spatial output, color boost, dynamic layout tests (50/500), black-bar classification and boot-hook invocation tested. Other models are unsupported until tested. Autostart symlink enable/disable, exact legacy migration, rejection of foreign files/symlinks, temporary-file collision and missing-target removal passed in an isolated ARM test on the TV. Version 0.6.1 was installed and started on the G4; the existing copied production hook migrated to the correct container symlink. Boot script syntax passed on the TV. Actual cold boot and clean-state installation/removal have not yet been demonstrated. Public listing eligibility remains pending human and DRM review, release URL validation and licence choice. The runtime reports Rockhopper 10.2.2-57 and device SoC o24; these measured values are used for the package requirements rather than an unverified minimum release.
