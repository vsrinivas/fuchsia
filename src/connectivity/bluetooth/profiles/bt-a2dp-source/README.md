# Bluetooth Profile: A2DP Source

This component implements the Advanced Audio Distribution Profile (A2DP) as
specified by the Bluetooth SIG in the [official specification](https://www.bluetooth.org/docman/handlers/downloaddoc.ashx?doc_id=457083).

This means that you can use your Fuchsia device to play music to Bluetooth
headphones or speakers.

This profile currently only supports the mandatory SBC audio codec.

## Build Configuration

The `bt-a2dp-source` component needs access to the following at runtime:
  - `fuchsia.media.AudioDeviceEnumerator` API to create the audio output device
     for source audio.  This is generally available on most Fuchsia devices,
     provided by the `audio_core` package.
  - `fuchsia.media.CodecFactory` API to encode the audio.  This is provided by
     the `codec_factory` package.
  - The codec factory must be able to encode SBC audio. This is currently
     provided in the `codec_runner_sw_sbc` package.

Without too many extra dependencies, adding the `audio` bundle and
`media_codec_sw_sbc` bundle to the available packages will provide all the
required services so adding the following to your Fuchsia set configuration
should build them all and make them available:

`--with //src/connectivity/bluetooth/profiles/bt-a2dp-source --with //src/media/audio/bundles:audio --with //garnet/packages/prod:media_codec_sw_sbc`

The profile makes an effort to determine if encoding media will fail, and quits
with a message on startup.

## Setting up Bluetooth Audio

To provide Audio through the bt-a2dp-source component, start the component,
then connect and pair to headphones or a speaker.  Here are the steps:

1. Start `bt-a2dp-source` with `run -d bt-a2dp-source.cmx`
1. If you have never paired with this device:
    1. Run bt-pairing-tool in a new fuchsia shell.
    1. Place the headphones/speaker in pairing mode (usually holding the power when turning it on will do this)
    1. If a prompt appears on the pairing tool in the next step, confirm it.
1. Connect to the headphones/speaker:
    1. Run `bt-cli`
    1. Run `start-discovery` and wait a few seconds.
    1. Run `list-peers` and look for your device.  If you know the name of the device, you can filter the results by adding part of the name as an argument.
    1. Run `stop-discovery` when your device shows up.
    1. Wait 10 seconds. (this is a bug workaround and will not be necessary soon)
    1. Run `connect <BT address>` - you can type a partial address and use tab completion
1. You should be connected to the headphones/speaker - you may hear a tone on them to confirm.
1. You should be able to play some audio on Fuchsia and hear it from your Bluetooth device:
    - `signal_generator` will produce a tone for a couple seconds.
    - `tiles_ctl start; tiles_ctl add http://youtube.com` will replace any scenic with a youtube browser.
    - Any other method of producing audio should also work.
1. Dance 💃

Troubleshooting:

  * If the bt-a2dp-source component doesn't start, make sure the bt-a2dp-sink component isn't also running.
    They can not simultaneously run. Consider stopping it with `killall bt-a2dp-sink.cmx`
  * Volume low? Try turning up the volume on your speaker, or using `vol` to turn up the volume on Fuchsia
  * Connecting from the headphones / speaker to the Fuchsia device currently does not work (bug 2783),
    you have to connect through bt-cli or another connection method for now.

