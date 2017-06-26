# Flutter-based Media Player Example App

This directory contains an application that uses apps/media and Flutter to
create a media player.

## USAGE

Run the app as follows:

    launch media_player_flutter

The initial screen is a menu of content items that can be played, unless the
config file contains only one asset, in which case the chooser screen is
skipped. Touch one of the items to play it. Note that the default config file
contains only one item.

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
        "album": "The Red Hot Chili Peppers",
        "loop": "true"
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
        "title": "Control player on myacer",
        "device": "myacer",
        "service": "media_player",
      },
    ]

The config file must be well-formed JSON specifying an array of objects. An
object describes a movie, a song, a playlist of movies and songs or a 'remote'.
A remote allows the user to control a player that's already running on another
device.

The following object fields are supported:

### uri (or url)

Specifies the URI from which to obtain the content. The file scheme is
supported for files on the device. This field is required for movies and songs
and prohibited for playlists and remotes.

### children

Specifies an array of items to populate this item, which must be a playlist.
This field is required for playlists and prohibited for all other item types.

### type

Specifies the type of the item, one of "movie", "music", "playlist" or "remote".
This field is optional. If it's absent, the app will attempt to infer the type
of the content.

### title

The title of the item. This field is optional. If it's absent, the item will be
titled "(untitled)" in the content chooser. During playback, the app will
attempt to get the title from the content, if applicable.

### artist

The name of the artist who created the content. This field is optional.

### album

The name of the album on which the content appears. This field is optional.

### loop

Whether the item should be looped. This field is optional and doesn't apply to
remotes. Playlists can be looped.

### device

Specifies the name of the device (in the NetConnector sense) for a remote. This
field is required for remotes and prohibited for all other types.

### service

Specifies the name of the service (in the NetConnector sense) for a remote. This
field is required for remotes and prohibited for all other types.

## FORMAT SUPPORT

* Containers
  * MATROSKA (MKV)
  * OGG
  * FLAC
  * WAV
* Encodings
  * THEORA
  * VP3
  * VP8
  * VORBIS
  * FLAC
  * PCM (various)
