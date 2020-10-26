# Bluetooth Profile: AVRCP Target

The AVRCP Target component is the intermediary between the core [AVRCP](https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/connectivity/bluetooth/profiles/bt-avrcp/) component and active media on the device. The component provides a distinct abstraction between the business logic of AVRCP and the state of the currently playing media. AVRCP Target subscribes to updates about registered media sessions and relays the information to the core AVRCP component.

## Build Configuration

Follow the steps for the [AVRCP](https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/connectivity/bluetooth/profiles/bt-avrcp/#build-configuration) build configuration and add `--with //src/connectivity/bluetooth/profiles/bt-avrcp-target` to include AVRCP-Target in your build.

To run the component:

1. `fx shell`
1. `run bt-avrcp-target.cmx`

## Running Tests

AVRCP Target relies on unit tests to validate behavior. To run the tests, add `--with //src/connectivity/bluetooth/profiles/bt-avrcp-target:tests` to your build.

To run the tests:
```
fx test bt-avrcp-target-tests
```

## Code Layout

The code is split into the aforementioned AVRCP-Media abstraction.

## `media` mod

* Responsible for listening to and updating media-related state.
* Processes updates from the currently active MediaSession using the [`fuchsia.media.sessions2`](https://fuchsia.googlesource.com/fuchsia/+/HEAD/sdk/fidl/fuchsia.media.sessions2/) FIDL service.
* Populates the thread-safe `MediaSessions` object with changes in state.
* `MediaSessions` provides an interface to query and modify relevant information about the current session.

## `avrcp_handler mod`

* Registers a new `TargetHandler` using the [`fuchsia.bluetooth.avrcp.PeerManager`](https://fuchsia.googlesource.com/fuchsia/+/HEAD/sdk/fidl/fuchsia.bluetooth.avrcp/controller.fidl) FIDL service.
* Processes requests over the [`fuchsia.bluetooth.avrcp.TargetHandler`](https://fuchsia.googlesource.com/fuchsia/+/HEAD/sdk/fidl/fuchsia.bluetooth.avrcp/target.fidl) FIDL protocol.
* Queries `MediaSessions` to get the latest information about the session.
* Updates AVRCP with status, control, and notification changes.
