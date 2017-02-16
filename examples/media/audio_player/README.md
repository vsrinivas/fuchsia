# Audio Player Example App

This directory contains an application that uses apps/media to create an audio
player.

## USAGE

The audio player uses a file reader or a network reader. To use the file
reader, you'll need to have an accessible file. Here's an example command line:

    @ bootstrap -
    @boot audio_player --path=/data/audio.ogg

Here's an example using the network reader:

    @boot audio_player --url=http://example.com/audio.ogg

It's important to use @boot as shown above rather than creating a new
bootstrap environment for each invocation of audio_player. Each instance of
the bootstrap environment will create its own instance of audio_server, and
multiple audio_servers will conflict when trying to access devices.

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
