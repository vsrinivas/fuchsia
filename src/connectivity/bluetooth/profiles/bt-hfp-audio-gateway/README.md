# Bluetooth Profile: Hands-Free Profile Audio Gateway (AG)

This component implements the Audio Gateway role of Hands-Free Profile (HFP) version 1.8 as
specified by the Bluetooth SIG in the
[official specification](https://www.bluetooth.org/DocMan/handlers/DownloadDoc.ashx?doc_id=489628).

This means that you can use your Fuchsia device to connect with Bluetooth to a headset with speakers
and microphone that supports the Hands-Free role. Audio data is streamed between the two devices
in real-time to support interactive voice calls.

HFP includes support for user-initiated actions on the Hands-Free device, such as answering calls,
hanging up calls, and volume control. See the specification for a complete list of functionality.

## Build Configuration

Add the following to your Fuchsia set configuration to include the profile component:

`--with //src/connectivity/bluetooth/profiles/bt-hfp-audio-gateway`

To run the CFv1 component:

1. `fx shell`
1. `run bt-hfp-audio-gateway.cmx`

### Profile Startup

Component startup differs based on the component framework version.

For CFv1, there are two ways that the HFP AG profile can be started on a fuchsia system:
automatically started on boot, or through protocol discovery.

When started through protocol discovery, the profile will not be started until the
`fuchsia.bluetooth.hfp.Hfp` FIDL capability is requested. Include the `service_config` target in
your fuchsia build set, for example by using
`--with //src/connectivity/bluetooth/profiles/bt-hfp-audio-gateway:service_config` on an `fx set`
line, or by depending on it alongside the bt-hfp-audio-gateway component in your product config
target.

To start the HFP AG profile automatically on startup include the `startup_config` target in your
fuchsia build set by using `--with //src/connectivity/bluetooth/profiles/bt-hfp-audio-gateway:startup_config`
on an `fx set` line, or by depending on it alongside the bt-hfp-audio-gateway component in your
product config target.

For CFv2, the only way to start the component is via protocol discovery. Include the `core_shard` in
your product config target (e.g add `//src/connectivity/bluetooth/profiles/bt-hfp-audio-gateway:bt-hfp-audio-gateway-core-shard`).
When the `fuchsia.bluetooth.hfp.Hfp` FIDL capability is requested, the CFv2 HFP AG component will be
started.

## Running tests

HFP relies on unit tests to validate behavior. Add the following to your Fuchsia set configuration
to include the profile unit tests:

`--with //src/connectivity/bluetooth/profiles/bt-hfp-audio-gateway:bt-hfp-audio-gateway-tests`

To run the tests:

```
fx test bt-hfp-audio-gateway-tests
```
