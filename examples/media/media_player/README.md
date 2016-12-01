# Media Player Example App

This directory contains an application that uses apps/media and apps/mozart to
create a media player.

## USAGE

The media player currently uses a file reader only, so you'll need to have an
accessible file. Here's an example command line:

  @ bootstrap -
  @boot launch media_player --path=/data/vid.ogv

In the future, we'll support a network reader as well.

It's important to use @boot as shown above rather than creating a new
bootstrap environment for each invocation of media_player. Each instance of
the bootstrap environment will create its own instance of audio_server, and
multiple audio_servers will conflict when trying to access devices.

The app responds to mouse clicks (touch on the Acer) and the keyboard. Mozart
requires a touch to focus the keyboard. Touching anywhere but the progress bar
toggles between play and pause. Touching the progress bar does a seek to that
point. The space bar toggles between play and pause. 'q' quits.

## AUDIO SUPPORT

Audio output works on e.g. Acer with a supported USB audio device. We currently
have no drivers for non-USB audio devices. The media player will play audio-only
files.

## HOW TO GET CONTENT ONTO YOUR MACHINE

The media player works best pulling content from a minfs volume, typically
mounted as /data. To get this volume set up, see the "Target Device" section
of https://fuchsia.googlesource.com/magenta/+/master/docs/minfs.md.

To get your content onto /data, copy it from your host machine to your USB
drive and then (on the Fuchsia device) from the USB drive to /data. In order
to do this, the USB drive will need to work with thinfs so it shows up as
/volume/fat-0.

Thinfs only works with GPT-partitioned drives, so first make sure your drive
is so partitioned. The linux utility gdisk can set up a GPT partition table.

Thinfs is also picky about the dirty bit on the drive. A freshly-formatted
FAT32 partition should work fine as long as the drive is properly ejected from
the host machine. Thinfs seems to set the dirty bit itself, so you may need to
reformat every time you want to copy a file.

In any case, make sure you specify a file name in /data when you cp. If you
just cp to /data, the root directory gets overwritten and life is bad.

    cp /volume/fat-0/vid.ogv /data/vid.ogv

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
