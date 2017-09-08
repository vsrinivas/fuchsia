# Media Player Example App

This directory contains an application that uses media and mozart to
create a media player.

## USAGE

The media player uses a file reader or a network reader. To use the file
reader, you'll need to have an accessible file. Here's an example command line:

    launch media_player /data/vid.ogv

Paths must be absolute (start with a '/'). Here's an example using the network
reader:

    launch media_player http://example.com/vid.ogv

The '--stay' option may be used if you want the player to start up with no
content and wait for a remote controller.

By default, the media player is exposed via NetConnector under the service name
'media_player'. The 'service' option can be used to change this:

    launch media_player --service=special_media_player <path or url>

or

    launch media_player --service=special_media_player --stay

The app can control a remote media player using the '--remote' option:

    --remote=myacer#media_player

In this case, the device name of the remote media player (as NetConnector
understands device names) is 'myacer', and the service name for the media
player is 'media_player'.

Note that the '--service' and '--remote' options are mutually exclusive. Unless
you're using '--stay', A url or path must be supplied for local playback and
may be supplied for remote control.

The app responds to mouse clicks (touch on the Acer) and the keyboard. Mozart
requires a touch to focus the keyboard. Touching anywhere but the progress bar
toggles between play and pause. Touching the progress bar does a seek to that
point. The space bar toggles between play and pause. 'q' quits.

## FORMAT SUPPORT

Using the default ffmpeg profile, the following formats are supported.

* Containers
  * MATROSKA (MKV)
  * OGG
  * FLAC
  * WAV
* Encodings
  * THEORA
  * VP3
  * VP8
  * VP9
  * VORBIS
  * FLAC
  * PCM (various)
