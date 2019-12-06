# Bluetooth Profile: A2DP Sink

This component implements the Advanced Audio Distribution Profile (A2DP) as
specified by the Bluetooth SIG in the [official specification](https://www.bluetooth.org/docman/handlers/downloaddoc.ashx?doc_id=457083).  This means that you can use your Fuchsia device with
audio support as a bluetooth speaker or (very not-portable) headphones.

This profile currently only supports the mandatory SBC audio decoder.

## Build Configuration

The `bt-a2dp-sink` component requires some services to be reachable when it is
run to be useful.  For audio playback, a provider of the `fuchsia.media.playback.Player`
service must be available. In current Fuchsia the `mediaplayer` package located
at `//src/media/playback/mediaplayer` is one provider.

The Player must in turn be able to decode SBC audio and play it. Currently SBC decoding
is provided internally if you are using the `mediaplayer` package.  For most players to
play audio, the `fuchsia.media.Audio` and `fuchsia.media.AudioCore` services will be used.

Without too many extra dependencies, adding the `audio` package group and `mediaplayer` package
to the available packages will provide all the required services so adding the following to your
Fuchsia set configuration should build them all and make them available:

`--with //src/connectivity/bluetooth/profiles/bt-a2dp-sink --with //src/media/audio/bundles:services --with //src/media/playback/mediaplayer`

The profile makes an effort to determine if playing media will fail, and crash with a log message
on startup.

## Listening to Bluetooth Audio

To listen to Audio through the bt-a2dp-sink component, start the component, connect and pair
from an audio source.

1. Start `bt-a2dp-sink` with `run -d fuchsia-pkg://fuchsia.com/bt-a2dp-sink#meta/bt-a2dp-sink.cmx`
1. (if you have never paired this device) Make the fuchsia device discoverable:
    - Run `bt-cli`
    - Use the `discoverable` command within the CLI to make the fuchsia device discoverable
    - Keep the `bt-cli` running until you have finished connecting
1. Connect and pair to the Fuchsia device from your source
1. Done! You should be able to play audio on the Fuchsia device
1. Dance 💃

Troubleshooting:

  * Make sure your audio system works - try using the `tones` test package.
  * Turn up the volume on the source device
  * Turn up the volume on the Fuchsia device (try using the `vol` command)
