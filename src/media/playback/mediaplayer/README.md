Mediaplayer
===========

Mediaplayer is a service for playback of audio and video streams from a variety
of sources and formats.

It implements the following FIDL services:

```
fuchsia.media.playback.Player
fuchsia.media.SessionAudioConsumerFactory
```

## Example

Use the `mediaplayer_test_util` app to playback a file using the mediaplayer
service.

For this example include the following in your build:

```
//src/media/playback/mediaplayer:tests
//src/media/playback/bundles:services
//src/media/playback/bundles:config
```

Then run the following on your host machine:

```
FILE=some_movie.mp4
fx shell killall basemgr.cmx
fx shell mkdir /tmp/r/sys/fuchsia.com:mediaplayer_test_util:0#meta:mediaplayer_test_util.cmx
fx cp --to-target $FILE /tmp/r/sys/fuchsia.com:mediaplayer_test_util:0#meta:mediaplayer_test_util.cmx/tmpmediafile
fx shell present_view fuchsia-pkg://fuchsia.com/mediaplayer_test_util#meta/mediaplayer_test_util.cmx --play /tmp/tmpmediafile
```

