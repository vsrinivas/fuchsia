# Bluetooth Profiles

The components here support various Bluetooth Profiles [defined by the
Bluetotoh SIG](https://www.bluetooth.com/specifications/profiles-overview/).

## Audio Profiles

The Advanced Audio Distribution Profile in Fuchsia cannot be operated in both
sink (receiving audio from a Bluetooth peer) and source (sending audio to a
peer) at the same time due to limitations on the lower level profile advertisement and discovery.

Products that want to integrate Bluetooth A2DP should choose between the
following options.

### A2DP sink exclusively

To operate as an A2DP sink exclusively (for example, for a speaker or
headphones), include these packages in your configuration:

```
//src/connectvity/bluetooth/profiles/bt-a2dp-sink
//src/connectvity/bluetooth/profiles/bt-a2dp-sink:startup_config
//src/media/audio/bundles:services
//src/media/playback/mediaplayer
//src/media/playback/mediaplayer:audio_consumer_config
```

For more information about build configuration with A2DP sink, see [the profile
documentation](bt-a2dp-sink/README.md).

### A2DP source exclusively

To operate as an A2DP sink exclusively (for example, for a speaker or
headphones), include these packages in your configuration:

```
//src/connectvity/bluetooth/profiles/bt-a2dp-source
//src/connectvity/bluetooth/profiles/bt-a2dp-source:startup_config
//src/media/audio
//src/media/codec:codec_factory
//src/media/codec:codec_runner_sw_sbc
```

For more information about build configuration with A2DP source, see [the profile
documentation](bt-a2dp-source/README.md).

Note that the startup config will also start the `bt-avrcp-target`
component if it is included.

### Use A2DP manager to switch between profiles

The [AudioMode](/sdk/fidl/fuchsia.bluetooth.a2dp/audio_mode.fidl) API is
provided to switch between the source and sink roles for Bluetooth Audio if a
product may need both roles (but not simultaneously).

When switching, the previous role is stopped.  Any peers connected to it are
disconnected from the audio, but are _not_ disconnected from the piconet.

Peers can be disconnected using the [Control](/sdk/fidl/fuchsia.bluetooth.control/control.fidl) API.

One AudioMode API provider is included here as `bt-a2dp-manager`.  To include
this service and the required components to run sink and source, add these packages
in your configuration:

```
//src/connectivity/bluetooth/profiles/bt-a2dp-manager
//src/connectivity/bluetooth/profiles/bt-a2dp-manager:startup_config
//src/connectvity/bluetooth/profiles/bt-a2dp-sink
//src/media/audio/bundles:services
//src/media/playback/mediaplayer
//src/media/playback/mediaplayer:audio_consumer_config
//src/connectvity/bluetooth/profiles/bt-a2dp-source
//src/media/audio
//src/media/codec:codec_factory
//src/media/codec:codec_runner_sw_sbc
```

`bt-a2dp-manager` does not start any profile automatically, and will need to be
called with an initial role after system startup to make one available.

#### Switching Profiles on the Command Line

The bt-a2dp-manager component can operate as a client of the `AudioMode` service when run with a
command line argument:

```
$ run bt-a2dp-manager source  # Set mode to source role.
$ run bt-a2dp-manager sink  # Set mode to sink role.
```
