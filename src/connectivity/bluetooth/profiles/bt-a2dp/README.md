# Bluetooth Profile: A2DP

This component implements the Advanced Audio Distribution Profile (A2DP) as
specified by the Bluetooth SIG in the [official specification](https://www.bluetooth.org/docman/handlers/downloaddoc.ashx?doc_id=457083).

This means that you can use your Fuchsia device to play audio to Bluetooth
headphones or speakers, or you can play audio from a phone or computer to your Fuchsia device,
or both at once.

This profile requires the SBC codec and will not start if it cannot decode/encode SBC audio.
It will also use and prefer the AAC codec for higher quality audio if it is available on the system.

## Build Configuration

The `bt-a2dp` component uses the following services at runtime:
  - `fuchsia.media.AudioDeviceEnumerator` API to create the audio output device
     for source audio.  This is generally available on most Fuchsia devices,
     provided by the `audio_core` package.
  - `fuchsia.media.CodecFactory` API to encode and decode audio. This is usually provided by
     the `codec_factory` package.
  - The codec factory must be able to encode SBC audio. This is currently
     provided in the `codec_runner_sw_sbc` package.

Without too many extra dependencies, adding the `audio`, `codec_factory`, and
`codec_runner_sw_sbc` packages to the available packages will provide all the
required services. Adding the following to your Fuchsia set configuration
should build them all and make them available:

`--with //src/connectivity/bluetooth/profiles/bt-a2dp --with //src/media/audio --with //src/media/codec:codec_factory --with //src/media/codec:codec_runner_sw_sbc`

The profile attempts to determine if encoding SBC audio will fail, and quits with a message on
startup if it cannot.

## Inspection

The `bt-a2dp.cmx` component implements
[component inspection](https://fuchsia.dev/fuchsia-src/development/diagnostics/inspect).
To view the current state of the profile, use `fx iquery show bt-a2dp.cmx`.

### Hierarchy

```
 root:
      connected:
        [below repeated for each connected peer]
        peer_0:
          id = 2c1044bce7b57143
          local_streams:
            stream_1:
              endpoint_state = StreamEndpoint { id: 5, [...] }
            stream_2:
              endpoint_state = StreamEndpoint { endpoint_type: Sink, media_type: Audio, state: Idle, capabilities: [MediaTransport, MediaCodec { media_type: Audio, codec_type: MediaCodecType::AUDIO_SBC, codec_extra: [63, 255, 2, 250] }], remote_id: None, configuration: [] }
            [repeated for each stream endpoint, connected enpoints will have a media_stream]
            stream_4:
              endpoint_state = [...]
              media_stream:
                bytes_per_second_current = 23672
                start_time = 11227039005541
                total_bytes = 125695
```

## Setting up Bluetooth Audio

### A2DP Source

To play audio from Fuchsia to a Bluetooth speaker or headphones through the bt-a2dp component,
start the component, then connect and pair to headphones or a speaker.

1. Start `bt-a2dp` with `run -d fuchsia-pkg://fuchsia.com/bt-a2dp#meta/bt-a2dp.cmx`
1. If you have never paired with this device:
    1. Run bt-pairing-tool in a new fuchsia shell.
    1. Place the headphones/speaker in pairing mode
       (usually holding the power when turning it on will do this)
    1. If a prompt appears on the pairing tool in the next step, confirm it.
1. Connect to the headphones/speaker:
    1. Run `bt-cli`
    1. Run `start-discovery` and wait a few seconds.
    1. Run `list-peers` and look for your device.
       If you know the name of the device, you can filter the results by adding part of the name
       as an argument.  Repeat until your device shows up.
    1. Run `stop-discovery` when your device shows up to stop discovery.
    1. Wait 10 seconds. (this is a (known bug)[http://fxbug.dev/2758] and will not be necessary soon)
    1. Run `connect <BT address>` - you can type a partial address and use tab completion
1. You should be connected to the headphones/speaker - you may hear a tone on them to confirm.
1. You should be able to play some audio on Fuchsia and hear it from your Bluetooth device:
    - `signal_generator` will produce a tone for a couple seconds.
    - Any other method of producing audio should also work.
1. Dance 💃


### A2DP Sink

To play audio on the Fuchsia device from a Bluetooth source (phone or computer),
start the component, then connect from a phone or computer.

1. Start `bt-a2dp` with `run -d fuchsia-pkg://fuchsia.com/bt-a2dp#meta/bt-a2dp.cmx`
1. (if you have never paired this device) Make the fuchsia device discoverable:
    - Run `bt-cli`
    - Use the `discoverable` command within the CLI to make the fuchsia device discoverable
    - Keep the `bt-cli` running until you have finished connecting
1. Connect and pair to the Fuchsia device from your source
1. Done! You should be able to play audio on the Fuchsia device
1. Dance 💃

Troubleshooting:
  * Volume low? Try turning up the volume on your source, using `vol` to turn up the volume on
    the Fuchsia device, or turning up the volume at the speaker.