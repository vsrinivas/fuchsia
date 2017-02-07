# Flutter-based Media Player Example App

This directory contains an application that uses apps/media and Flutter to
create a media player.

## USAGE

Run the app as follows:

    @ bootstrap -
    @boot launch media_player_flutter

It's important to use @boot as shown above rather than creating a new
bootstrap environment for each invocation of media_player_flutter. Each instance
of the bootstrap environment will create its own instance of audio_server, and
multiple audio_servers will conflict when trying to access devices.

Flutter apps can't currently process command line parameters, and supporting
text entry is problematic. For these reasons, media_player_flutter relies on
a config file to determine what content to play. The app allows the user to
choose from a number of playable items.

The app will look for config files in two places, reading only the first file
it finds:

- /data/media_player_flutter.config
- /system/data/media_player_flutter/media_player_flutter.config

Here's an example config file:

    [
      {
        "uri": "http://192.168.4.1/big_buck_bunny.ogv",
        "title": "Big Buck Bunny"
      },
      {
        "uri": "http://192.168.4.1/policehelicopter.ogg",
        "title": "Police Helicopter",
        "artist": "The Red Hot Chili Peppers",
        "album": "The Red Hot Chili Peppers"
      },
      {
        "uri": "http://192.168.4.1/superstition.ogg",
        "title": "Superstition",
        "artist": "Stevie Wonder",
        "album": "At the Close of the Century Disc 2"
      },
      {
        "title": "Both Songs",
        "children": [
          {
            "uri": "http://192.168.4.1/policehelicopter.ogg",
            "title": "Police Helicopter",
            "artist": "The Red Hot Chili Peppers",
            "album": "The Red Hot Chili Peppers"
          },
          {
            "uri": "http://192.168.4.1/superstition.ogg",
            "title": "Superstition",
            "artist": "Stevie Wonder",
            "album": "At the Close of the Century Disc 2"
          }
        ]
      },
      {
        "uri": "remoteplayer://myacer/only_media_player",
        "title": "Remote player on myacer",
      },
    ]

The config file must be well-formed JSON specifying an array of objects. The
following object fields are supported:

### uri (or url)

Specifies the URI from which to obtain the content. The file scheme is
supported for files on the device. This field is prohibited for playlists and
required for all other item types.

To control a remote player that's already running, use a URI of the form
"remoteplayer://<device>/<service>" where <device> is the name of the device
on which the remote player is running and <service> is the service name with
which the player is registered. Currently, all media players are registered
under the service name "only_media_player".

### children

Specifies an array of items to populate this item, which must be a playlist.
This field is required for playlists and prohibited for all other item types.

### type

Specifies the type of the item, one of "movie", "music", "playlist" or "remote".
This field is optional. If it's absent, the app will attempt to infer the type
of the content.

### title

The title of the item. This field is optional. If it's absent, the item will be
titled "(untitled)" in the content chooser. The app will attempt to get the
title from the content, if applicable.

### artist

The name of the artist who created the content. This field is optional.

### album

The name of the artist who created the content. This field is optional.

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
