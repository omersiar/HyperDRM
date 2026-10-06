# Runtime boundaries

The native service remains one translation unit because LG symbols are resolved
at runtime. Its implementation headers share the context declared in service.c;
they are included once in dependency order, not independent public libraries.
Formatting must preserve include order (`SortIncludes: Never`).

| Module | Responsibility |
| --- | --- |
| service.c | Runtime context, LG/Luna loading, registration and shutdown |
| service_api.h | Start/stop, root restart, settings and autostart actions |
| service_calibration.h | Bounded calibration lease, proposals, profiles, model discovery |
| service_status.h | Read-only status serialization |
| capture_loop.h | Scheduler, histogram acquisition, missing-data recovery and output |
| receiver_stream.h | Independent, nonblocking receiver LED subscription |
| source_bridge.h | Bounded color continuity during source transitions |
| handover.h | Exclusive PicCap/histogram capture ownership |

Connection stages are explicitly named in runtime_states.h:
Disconnected → Connecting → Authenticating (when configured) → Layout → Subscribed.
Any connection, authorization or parsing failure closes the connection and
schedules a bounded retry. Up to eight buffered messages are consumed per tick,
including ticks where no new bytes arrive. Buffer capacity and read budgets remain
bounded. LED count/profile changes trigger a fresh layout subscription.

The color bridge uses Idle → Holding → WaitingFrame → Fading → Idle. It holds the
last histogram frame while PicCap starts, clears its priority before observing a
fresh PicCap frame, then blends for 500 ms. The transition expires after 3 seconds;
real black bypasses the fade. The opposite direction seeds histogram continuity
from a fresh receiver frame. Capture ownership remains in handover.h; the observer
never acquires LG capture. Stop cancels the bridge and releases owned resources.

The UI calibration session uses Starting → Waiting → Measuring/Validating →
Committing → finished. Each asynchronous callback checks its original session
identity, so a cancelled session cannot modify a newly started session. Proposals
remain unsaved until the user selects Save profile. The native lease still bounds
the session if the UI disappears.

UI action errors take precedence over transient status errors. Polling cannot
erase them; starting the next action clears them. Receiver settings, capture state,
calibration state and presentation errors remain distinct.

VERSION generates app/package metadata and native/version.h. release.py tests
portable logic locally, builds the ARM service and verifies the exact package.
Hardware capture and Magic Remote behavior still require a later TV check; a host
test is not evidence of LG hardware behavior.
