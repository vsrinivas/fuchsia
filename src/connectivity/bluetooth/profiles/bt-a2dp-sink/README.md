# Bluetooth Profile: A2DP Sink

This component implements the Advanced Audio Distribution Profile (A2DP) as
specified by the Bluetooth SIG in the [official specification](https://www.bluetooth.org/docman/handlers/downloaddoc.ashx?doc_id=457083).

It implements an Audio Sink, which means that you can use your Fuchsia device
with audio support as a Bluetooth speaker or headphones.

This profile supports both the SBC audio codec and the AAC audio codec.  If
your source device supports AAC, you are likely to get high quality audio.

## Build Configuration

The `bt-a2dp-sink` component requires some services to be reachable when it is
run to be useful.  For audio playback, a provider of the
`fuchsia.media.SessionAudioConsumerFactory` service must be available. One provider of
this currently in Fuchsia is the `mediaplayer` package located at `//src/media/playback/mediaplayer` 
is one provider, but the config must also be provided.

The Player must in turn be able to decode audio and play it. Currently SBC
decoding is provided internally if you are using the `mediaplayer` package.
For most players to play audio, the `fuchsia.media.Audio` and
`fuchsia.media.AudioCore` services will be used.

Without too many extra dependencies, adding the `services` bundle from audio,
the `mediaplayer` package and the audio consumer config package to the available 
packages will provide all of these requirements. 

Adding the following to your Fuchsia set configuration should build them all and make
them available:

`--with //src/connectivity/bluetooth/profiles/bt-a2dp-sink --with //src/media/audio/bundles:services --with //src/media/playback/mediaplayer --with-base=//src/media/playback/mediaplayer:audio_consumer_config`

The profile makes an effort to determine if playing media will fail, and quits
with a message on startup.

## Listening to Bluetooth Audio

To listen to Audio through the bt-a2dp-sink component, start the component,
connect and pair from an audio source.

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
