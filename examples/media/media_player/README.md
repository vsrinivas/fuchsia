# Media Player Example App

This directory contains an application that uses apps/media and apps/mozart to
create a media player.

## USAGE

The media player uses a file reader or a network reader. To use the file
reader, you'll need to have an accessible file. Here's an example command line:

    launch media_player --path=/data/vid.ogv

Here's an example using the network reader:

    launch media_player --url=http://example.com/vid.ogv

The app responds to mouse clicks (touch on the Acer) and the keyboard. Mozart
requires a touch to focus the keyboard. Touching anywhere but the progress bar
toggles between play and pause. Touching the progress bar does a seek to that
point. The space bar toggles between play and pause. 'q' quits.

## AUDIO SUPPORT

Audio output works on e.g. Acer with a supported USB audio device. We currently
have no drivers for non-USB audio devices. The media player will play audio-only
files.

## FORMAT SUPPORT

* Containers
  * MATROSKA (MKV)
  * OGG
  * WAV
* Encodings
  * THEORA
  * VP3
  * VP8
  * VORBIS
  * PCM (various)
