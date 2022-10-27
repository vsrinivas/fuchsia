# Audio Device Registry Service

The Audio Device Registry service enables clients to enumerate, observe and
control audio devices. It automatically detects audio devices that use the
device file system (`devfs`) namespaces `dev/class/audio-output` and
`dev/class/audio-input`, and it exposes interfaces to manually add audio devices
that do not use those namespaces.

## Implications of Being a Mechanism-Only Service

A significant motivation for extracting device-related functionality to a
dedicated service is to separate *mechanism* (taking actions) from *policy*
(making choices; instructing the mechanisms how to proceed).

To prepare a device for audio streaming, it is configured based on the
properties and possible settings it exposes. Mirroring the difference between
*mechanism* and *policy*, note our distinction between *initialization/querying*
(retrieving device state, enumerating capabilities) and *configuration*
(choosing and setting a configuration, from the possibilities previously
exposed).

`AudioDeviceRegistry` will be *mechanism-only*, so it will *detect and query*
devices while leaving *configuration* decisions to a higher-level policy
authority (such as a dedicated "audio policy" service). Having decided how to
configure the devices, this policy authority would instruct
`AudioDeviceRegistry` to make the necessary changes.

This suggests that generally there will be only one *direct* client for the
Audio Device Registry interfaces that change device state (see below:
`Controller`, `Control`, `RingBuffer`). These interfaces are designed with this
in mind.

This also suggests certain limitations in how *other* clients can use the
"listener" Audio Device Registry interfaces (see below: `Registry`, `Observer`).
The service uses these interfaces to expose devices after enumerating their
properties. Thus, any device that isn't actually *started* might be at any step
in the process of being configured. These interfaces cannot (for example) signal
when a particular device is configured but not yet started: only the entity that
configures audio devices can indicate when this is complete.

## Classes and Relationships

Shared pointers are used throughout the service. When an object id described as
"held" by another, this indicates the primary way it is accessed, rather than
strict ownership.

### AudioDeviceRegistry

`AudioDeviceRegistry` is the service's central class. A singleton object is
created in `main()` and exists for the lifetime of the process, holding shared
references to all other objects either directly or indirectly.
`AudioDeviceRegistry` kicks off device detection and serves incoming requests
for the three discoverable `fuchsia.audio.device` protocols: `Registry`,
`Controller`, and `Provider`.

These three FIDL protocols, and the additional three protocols that are created
from them, are implemented by classes with the same name, with 'Server'
appended. See the FIDL doc comments for details about the purpose of each
protocol.

### DeviceDetector

The `DeviceDetector` class is a singleton without other dependencies, created by
the `AudioDeviceRegistry` at service startup time. True to its name, this object
detects audio devices in the `devfs` namespace and provides them to the
`AudioDeviceRegistry` to be added.

### Device

The `Device` class represents audio devices that may or may not yet be in use or
even fully queried. The `AudioDeviceRegistry` holds a collection of these
objects. A `Device` object is created when it is detected (or manually added by
FIDL call), and `AudioDeviceRegistry` calls `Device/Initialize` to kick off the
discovery and enumeration of that device's capabilities. In addition to metadata
about the device's capabilities, this object holds most of the underlying FIDL
connections to the audio driver (`fuchsia.hardware.audio.StreamConfig`,
`fuchsia.hardware.audio.Health`,
`fuchsia.hardware.audio.signalprocessing.SignalProcessing`). This object also
holds all the connections used to observe or control the device
(`ObserverServer` or `ControlServer`, described below).

### RegistryServer and ControllerServer

`RegistryServer` objects and `ControllerServer` objects are held by the
`AudioDeviceRegistry`. Both classes interact with the list of published `Device`
instances through the parent `AudioDeviceRegistry`.

### ProviderServer

`ProviderServer` objects are also held by the `AudioDeviceRegistry`. Calls to
`fuchsia.audio.device.Provider/AddDevice` use the `AudioDeviceRegistry` function
that is called when the `DeviceDetector` detects an audio device.

### ObserverServer and ControlServer

`ObserverServer` objects and `ControlServer` objects are referenced by the
`Device` that they are observing/controlling. They are created by
`Registry/CreateObserver` or `Controller/Create` calls, but the `Registry` or
`Controller` that created them holds no reference to them.

### RingBufferServer

`RingBufferServer` objects are created by `Control/CreateRingBuffer` and exist
only as long as the parent `Control` exists. The parent `ControlServer` creates
this object immediately after creating the underlying
`fuchsia.hardware.audio.RingBuffer` connection to the audio driver.

## Rough Code Sketch

Below we describe the logic and actions taken at various service entry points.

### Upon service startup

The service will start upon the first incoming FIDL request on any of its
discoverable protocols (`Registry`, `Control`, `Provider`). The decision to
demand-start (rather than auto-start earlier in bootup) is discussed below,
after first describing what occurs during service startup.

When the service is started, it performs the following tasks:

1.  Start the ongoing **detection** of audio devices in `devfs`;
2.  Accept incoming FIDL requests to **observe**, **control**, or **manually
    add** audio devices;
3.  **Initialize**, **query** and **publish** devices as they are detected or
    manually added;

Step 1 is performed by a `DeviceDetector`, step 2 by the parent
`AudioDeviceRegistry`, and step 3 by the `Device` object created for each device
when it is detected/added.

Note: although a device's detection synchronously triggers its
initialization/querying, and although the service is single-threaded, this
service makes no guarantee that the initial `Registry/WatchDevicesAdded` call
will return any devices -- even on systems with built-in devices that are
immediately detected in `devfs`. Querying a device requires a number of
asynchronous driver responses, and we enforce no sequencing between the
*completion* of device querying and the *acceptance* of incoming `Registry`
requests (possibly with immediate `Registry/WatchDevicesAdded` calls).

Once the service's device detectors and FIDL handlers are started, the service
runs indefinitely. There is no current plan for the service to be stoppable and
restartable; this can be added easily if needed (`fuchsia.process.Lifecycle`
above).

### Upon device addition

When a device is added, the corresponding `Device` object is created, which
makes the following calls to fetch device metadata:

*   `StreamConfig/GetProperties` to fetch the unique ID, manufacturer and
    product strings, gain and plug capabilities, and clock domain;
*   `StreamConfig/GetSupportedFormats` to retrieve the complete set of formats
    that the device can support;
*   `StreamConfig/WatchGainState` to be notified of the initial gain state (and
    any subsequent gain changes);
*   `StreamConfig/WatchPlugState` to be notified of the initial plug state (and
    any subsequent plug changes).
*   `signal_processing.Reader/GetTopologies` to determine whether the driver
    supports the `SignalProcessing` protocol.

The initial call to underlying `fuchsia.hardware.audio.signal_processing.Reader`
interfaces could have been deferred until requested by an associated `Observer`,
but this is done initially to better understand the device's state and
capabilities, before it is exposed to the first `Observer`. All subsequent
checks can reference a single `supported` boolean.

Once the device enumeration/initialization calls have received the expected
responses from the driver (and after creating the device clock), the `Device` is
fully populated, and all `Registry` clients that are watching device arrivals
are notified of the new device.

### Upon creation of a client FIDL connection

The creation of `RegistryServer` / `ObserverServer` / `ControllerServer` /
`ControlServer` / `RingBufferServer` / `ProviderServer` instances refer to the
relevant `Device`(s), and every call to these interfaces is satisfied either
from `Device` metadata or calls to the underlying driver interfaces
(`StreamConfig`, `RingBuffer`, `Health`, `SignalProcessing`).

Creating a `Control` is a request to take exclusive control of the specified
audio device. If this is available and permitted, the request succeeds, marking
the `Device` as ***controlled***. If the device is *already* ***controlled***,
`Controller/Create` will fail. Closing a `Control` returns the `Device` to the
***uncontrolled*** available state.

`Control/CreateRingBuffer` creates the underlying
`fuchsia.hardware.audio.RingBuffer` along with the associated
`RingBufferServer`.

### Upon closure of client FIDL connection

Closing a `Control` or `RingBuffer` causes the underlying
`fuchsia.hardware.audio.RingBuffer` to be stopped and closed. Closing the others
(`Registry`, `Observer`, `Controller`, `Provider`) has no effect on device state
or any other FIDL connections (including children, e.g. `Control`).

### Upon closure of underlying driver FIDL connection

If the underlying `fuchsia.hardware.audio.RingBuffer` is closed, the associated
`RingBufferServer` and `ControlServer` objects are destroyed, and the `Device`
reverts to the ***uncontrolled*** state.

If the underlying `fuchsia.hardware.audio.StreamConfig` is closed, the `Device`
and all associated FIDL objects are destroyed. `RegistryServer`,
`ControllerServer` and `ProviderServer` objects are unaffected, other than
`Registry/WatchDeviceRemoved` callers being notified. `AudioDeviceRegistry` only
closes the `StreamConfig` in unrecoverable cases; although the device will still
exist in `devfs`, we do not attempt to redetect and re-add it.

### Driver health and device removal

During the initial querying of device capabilities, `Device` calls
`Health/GetHealthState`, to double-check that the driver is operational before
we expose the device to clients. This is done to avoid notifying clients of a
device that actually cannot be used. Driver health can change at any time, and
status is only discovered by polling. As a reasonable balance between
continually checking health versus never checking, `AudioDeviceRegistry` will
call `Health/GetHealthState` when creating `Observer`s, `Control`s and
`RingBuffer`s. If `GetHealthState` fails or times out, the "Create" calls will
fail with appropriate error, and the device will be removed with appropriate
notifications.

There is no mechanism to detect a device being removed from `devfs`. Other than
driver health, the only other event that leads us to remove a device is the
closure of the underlying driver `StreamConfig` FIDL connection.

As mentioned earlier (*Other interfaces that were considered -
RedetectDevices*), upon request we could choose to rerun the device-detection
and querying process for "unhealthy" devices. As concluded in that earlier
section however, this step in isolation will not likely be fruitful; we should
also either power-cycle the audio hardware, or kill and restart the driver
process.

## Alternatives that were considered

### Eager service startup

The audio device registry service could be marked as `eager startup` if early
device detection is desired. Component Framework documents
(https://fuchsia.dev/fuchsia-src/development/components/v2/migration/common?hl=en#start-on-boot,
https://fuchsia.dev/fuchsia-src/development/components/connect#eager) detail the
implications of being 'eager', but further detail is out of scope for this
design doc.

Note: whether this service is started eagerly is *orthogonal* to any guarantee
(or lack thereof) that the service makes about the devices listed in the
response to the first `WatchDevicesAdded` call.

### Indicating device-ready (or other states)

A client might be interested in when a particular device is ready. Although we
expect this type of signal to be provided instead by audio policy, nonetheless
`Observer/WatchStartState` could indicate to clients when a device ring buffer
has been started. Disclosing a device's "start" state is useful (it means the
device is configured, healthy and can be targeted), without revealing too much
to unprivileged observers (e.g. the ring buffer location). Although we do not
plan to include this method in the initial implementation, it could be ***easily
added*** if needed.

### Making `AudioDeviceRegistry` stoppable and restartable

Ideally, all Fuchsia services would be stoppable and restartable. The
`fuchsia.process.Lifecycle` protocol (specifically its lone method `Stop`) is
used to instruct services to unbind and exit. If `AudioDeviceRegistry`
implemented this, a client could call it and then restart the service by
reconnecting to any discoverable `AudioDeviceRegistry` interface. Although we do
not plan to include this interface in the initial implementation, this method
would be ***easily added*** if needed.

### Restarting unhealthy devices

Once an audio driver/device enters a bad state, we would ideally restart and
recover it. A method such as `RedetectDevices` (perhaps on a top-level parent
`AudioDeviceRegistry` or `DeviceManager` interface) could redetect and requery
devices that were previously marked as not properly operational. Specifically it
would stop and restart the long-running device watchers, which would know to
skip any detected devices that are already operational.

There are two possible underlying causes for device that is malfunctional or
unresponsive: (1) hardware is in a bad state; (2) driver is in a bad state. To
revive a device, the hardware may need to be *power-cycled*; to recover a
driver, its process may need to be stopped/restarted. These are both significant
actions that are outside the scope of this service. For these reasons, we do not
plan to include this method, since as described it is ***insufficient***.
`AudioDeviceRegistry` does not attempt to handle device recovery at all, leaving
this to upper layers.
