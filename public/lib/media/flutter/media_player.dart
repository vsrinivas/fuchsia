// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:apps.media.lib.flutter/media_player_controller.dart';
import 'package:apps.media.lib.flutter/progress_notifier.dart';
import 'package:lib.ui.flutter/child_view.dart';

import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';

/// Widget that plays media given a URL (including file: URLs).
class MediaPlayer extends StatefulWidget {
  /// The controller exposed by this widget.
  final MediaPlayerController controller;

  /// Constructs a [MediaPlayer] from an existing controller.
  MediaPlayer(this.controller, {Key key}) : super(key: key);

  @override
  _MediaPlayerState createState() => new _MediaPlayerState();
}

/// The state of a MediaPlayer widget.
class _MediaPlayerState extends State<MediaPlayer> {
  ProgressNotifier _secondsNotifier;
  ProgressNotifier _sliderNotifier;

  @override
  void initState() {
    assert(widget.controller != null);
    _hookController(widget.controller);
    super.initState();
  }

  @override
  void dispose() {
    _unhookController(widget.controller);
    super.dispose();
  }

  @override
  void didUpdateWidget(MediaPlayer oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.controller != widget.controller) {
      _unhookController(oldWidget.controller);
      _hookController(widget.controller);
    }
  }

  /// Adds notification hooks to |controller|.
  void _hookController(MediaPlayerController controller) {
    controller.addListener(_handleControllerChanged);
    _secondsNotifier = new ProgressNotifier(controller);
    _sliderNotifier = new ProgressNotifier(controller);
  }

  /// Removes notification hooks from |controller|.
  void _unhookController(MediaPlayerController controller) {
    controller.removeListener(_handleControllerChanged);
    _secondsNotifier?.dispose();
    _secondsNotifier = null;
    _sliderNotifier?.dispose();
    _sliderNotifier = null;
  }

  /// Handles change notifications from the controller.
  void _handleControllerChanged() {
    setState(() {});
  }

  /// Converts a duration to a string indicating seconds, such as '1:15:00' or
  /// '2:40'
  static String _durationToString(Duration duration) {
    int seconds = duration.inSeconds;
    int minutes = seconds ~/ 60;
    seconds %= 60;
    int hours = minutes ~/ 60;
    minutes %= 60;

    String hoursString = hours == 0 ? '' : '$hours:';
    String minutesString =
        (hours == 0 || minutes > 9) ? '$minutes:' : '0$minutes:';
    String secondsString = seconds > 9 ? '$seconds' : '0$seconds';

    return '$hoursString$minutesString$secondsString';
  }

  /// Gets the desired size of this widget.
  Size get _layoutSize {
    Size size = widget.controller.videoPhysicalSize;

    if (size.width == 0) {
      size = const Size(320.0, 45.0);
    } else {
      size = size / MediaQuery.of(context).devicePixelRatio;
    }

    return size;
  }

  /// Builds an overlay widget that contains playback controls.
  Widget _buildControlOverlay() {
    assert(debugCheckHasMaterial(context));

    return new Stack(
      children: <Widget>[
        new Center(
          child: new PhysicalModel(
            elevation: 2.0,
            color: Colors.transparent,
            borderRadius: new BorderRadius.circular(60.0),
            child: new IconButton(
              icon: widget.controller.problem != null
                  ? new Icon(Icons.error_outline)
                  : widget.controller.loading
                      ? new Icon(Icons.hourglass_empty)
                      : widget.controller.playing
                          ? new Icon(Icons.pause)
                          : new Icon(Icons.play_arrow),
              iconSize: 60.0,
              onPressed: () {
                if (widget.controller.playing) {
                  widget.controller.pause();
                } else {
                  widget.controller.play();
                }
              },
              color: Colors.white,
            ),
          ),
        ),
        new Positioned(
          left: 2.0,
          bottom: 8.0,
          child: new PhysicalModel(
            elevation: 2.0,
            color: Colors.transparent,
            borderRadius: new BorderRadius.circular(10.0),
            child: new AnimatedBuilder(
              animation:
                  _secondsNotifier.withResolution(const Duration(seconds: 1)),
              builder: (BuildContext context, Widget child) => new Container(
                width: 65.0,
                child: new Text(
                    _durationToString(widget.controller.progress),
                    style: new TextStyle(color: Colors.white),
                    textAlign: TextAlign.center,
                  ),
                ),
            ),
          ),
        ),
        new Positioned(
          left: 48.0,
          right: 48.0,
          bottom: 0.0,
          child: new PhysicalModel(
            elevation: 3.0,
            color: Colors.transparent,
            child: new LayoutBuilder(
              builder: (BuildContext context, BoxConstraints constraints) =>
                  new AnimatedBuilder(
                    animation: _sliderNotifier.withExcursion(
                        constraints.maxWidth, context),
                    builder: (BuildContext context, Widget child) => new Slider(
                        min: 0.0,
                        max: 1.0,
                        activeColor: Colors.red[900],
                        value: widget.controller.normalizedProgress,
                        onChanged: (double value) =>
                            widget.controller.normalizedSeek(value)),
                  ),
            ),
          ),
        ),
        new Positioned(
          right: 2.0,
          bottom: 8.0,
          child: new PhysicalModel(
            elevation: 2.0,
            color: Colors.transparent,
            borderRadius: new BorderRadius.circular(10.0),
            child: new Container(
              width: 65.0,
              child: new Text(
                _durationToString(widget.controller.duration),
                style: new TextStyle(color: Colors.white),
                textAlign: TextAlign.center,
              ),
            ),
          ),
        ),
      ],
    );
  }

  @override
  Widget build(BuildContext context) {
    return new AspectRatio(
      aspectRatio: _layoutSize.width / _layoutSize.height,
      child: new Stack(
        children: <Widget>[
          new GestureDetector(
            onTap: widget.controller.brieflyShowControlOverlay,
            child: new ChildView(
                connection: widget.controller.videoViewConnection),
          ),
          new Offstage(
            offstage: !widget.controller.shouldShowControlOverlay,
            child: _buildControlOverlay(),
          ),
        ],
      ),
    );
  }
}
