# Audio Player Example App

This directory contains an application that uses media to create an audio
player.

## USAGE

The audio player uses a file reader or a network reader. To use the file
reader, you'll need to have an accessible file. Here's an example command line:
```
    audio_player /data/audio.ogg
```
Here's an example using the network reader:
```
    audio_player http://example.com/audio.ogg
```
Given a path or URL to play, the audio player will terminate unless the
'service' option is used. The 'stay' option will also prevent the player from
terminating.
