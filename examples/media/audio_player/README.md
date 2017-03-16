# Audio Player Example App

This directory contains an application that uses apps/media to create an audio
player.

## USAGE

The audio player uses a file reader or a network reader. To use the file
reader, you'll need to have an accessible file. Here's an example command line:

    audio_player --url=file:///data/audio.ogg

Here's an example using the network reader:

    audio_player --url=http://example.com/audio.ogg

By default, the audio player is exposed via NetConnector under the service name
'audio_player'. The 'service' option can be used to change this:

    audio_player --service=special_audio_player

## AUDIO SUPPORT

Audio output works on e.g. Acer with a supported USB audio device. We currently
have no drivers for non-USB audio devices. The media player will play audio-only
files.

## FORMAT SUPPORT

* Containers
  * OGG
  * WAV
* Encodings
  * THEORA
  * PCM (various)
