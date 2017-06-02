# Flutter Library for Media

This directory contains dart code to help play media in a Flutter app. The
adjacent `lib\dart` directory contains more dart code that has no Flutter
dependency. This directory depends on `lib\dart`.

## MediaPlayerController

`MediaPlayerController` is a class that manages FIDL media players. This class
is distinct from the widget, because it typically needs to outlive the widget
state.

A controller instance controls a single media player, either on the local device
or on a remote device. It will create a local player, but it doesn't have any
way to create a remote player. A remote player must be started somehow, then
a `MediaPlayerController` will connect to it via the `connectToRemote` method.
To create a local player, call the `open` method.

Once a local or remote player is open, it can be controlled using various
methods. It also exposes properties that let the application know what's going
on. The controller implements `Listenable`, so applications can know when
its properties have changed.

## MediaPlayerController

`MediaPlayer` is a stateful widget that shows video and provides some UI such
as play/pause buttons and a touchable progress bar. It can also be used for
audio-only content, in which case only the UI is shown. Its constructor accepts
a `MediaPlayerController`.

The layout logic of a `MediaPlayer` attempts to preserve proper video aspect
ratio if possible. If both dimensions are constrained, the video will be
stretched accordingly. Otherwise, the widget will be as large as it can be
while meeting its imposed constraints and maintaining correct aspect ratio.
