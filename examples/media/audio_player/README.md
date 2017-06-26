# Audio Player Example App

This directory contains an application that uses apps/media to create an audio
player.

## USAGE

The audio player uses a file reader or a network reader. To use the file
reader, you'll need to have an accessible file. Here's an example command line:

    audio_player /data/audio.ogg

Here's an example using the network reader:

    audio_player http://example.com/audio.ogg

By default, the audio player is exposed via NetConnector under the service name
'audio_player'. The 'service' option can be used to change this:

    audio_player --service=special_audio_player

Given a path or URL to play, the audio player will terminate unless the
'service' option is used. The 'stay' option will also prevent the player from
terminating.

## FORMAT SUPPORT

* Containers
  * OGG
  * FLAC
  * WAV
* Encodings
  * VORBIS
  * FLAC
  * PCM (various)
