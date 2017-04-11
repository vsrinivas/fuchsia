// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:apps.media.lib.flutter/media_player_controller.dart';
import 'package:apps.mozart.lib.flutter/child_view.dart';

import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';

final Duration _kOverlayAutoHideDuration = const Duration(seconds: 3);
final Duration _kProgressBarUpdateInterval = const Duration(milliseconds: 100);

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
  Timer _hideTimer;
  Timer _progressTimer;

  @override
  void initState() {
    assert(widget.controller != null);
    widget.controller.addListener(_handleControllerChanged);
    if (_shouldShowControlOverlay()) {
      _ensureProgressTimer();
    } else {
      _ensureNoProgressTimer();
    }
    super.initState();
  }

  @override
  void dispose() {
    _ensureNoProgressTimer();
    _hideTimer?.cancel();
    widget.controller.removeListener(_handleControllerChanged);
    super.dispose();
  }

  @override
  void didUpdateWidget(MediaPlayer oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.controller != widget.controller) {
      oldWidget.controller.removeListener(_handleControllerChanged);
      widget.controller.addListener(_handleControllerChanged);
    }
  }

  /// Handles change notifications from the controller.
  void _handleControllerChanged() {
    setState(() {
      if (_shouldShowControlOverlay()) {
        _ensureProgressTimer();
      } else {
        _ensureNoProgressTimer();
      }
    });
  }

  /// Determines of the control overlay should be shown.
  bool _shouldShowControlOverlay() {
    return !widget.controller.hasVideo || !widget.controller.playing ||
      _hideTimer != null;
  }

  /// Shows the control overlay for [_kOverlayAutoHideDuration].
  void _brieflyShowControlOverlay() {
    setState(() {
      if (_hideTimer != null) {
        _hideTimer.cancel();
      }

      _hideTimer = new Timer(_kOverlayAutoHideDuration, () {
        setState(() {
          _hideTimer = null;
        });
      });

      _ensureProgressTimer();
    });
  }

  /// Ensure that the progress timer is running.
  void _ensureProgressTimer() {
    if (_progressTimer != null) {
      return;
    }

    _progressTimer = new Timer.periodic(
      _kProgressBarUpdateInterval,
      (Timer timer) => setState(() {})
    );
  }

  /// Ensure that the progress timer is not running.
  void _ensureNoProgressTimer() {
    if (_progressTimer == null) {
      return;
    }

    _progressTimer.cancel();
    _progressTimer = null;
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

  /// Returns progress as a value from 0.0 to 1.0 inclusive.
  double get _unitProgress {
    int durationInMicroseconds = widget.controller.duration.inMicroseconds;

    if (durationInMicroseconds == 0) {
      return 0.0;
    }

    return widget.controller.progress.inMicroseconds / durationInMicroseconds;
  }

  /// Seeks to a position given as a value from 0.0 to 1.0 inclusive.
  void _unitSeek(double unitPosition) {
    int durationInMicroseconds = widget.controller.duration.inMicroseconds;

    if (durationInMicroseconds == 0) {
      return;
    }

    widget.controller.seek(new Duration(
      microseconds: (unitPosition * durationInMicroseconds).round()
    ));
  }

  /// Gets the desired size of this widget.
  Size get _layoutSize {
    Size size = widget.controller.videoPhysicalSize;

    if (size.width == 0) {
      size = const Size(320.0, 45.0);
    } else {
      size = size /  MediaQuery.of(context).devicePixelRatio;
    }

    return size;
  }

  /// Builds an overlay widget that contains playback controls.
  Widget _buildControlOverlay() {
    assert(debugCheckHasMaterial(context));

    return new Stack(
      children: <Widget>[
        new Center(
          child: new IconButton(
            icon: widget.controller.problem != null ?
              new Icon(Icons.error_outline) :
              widget.controller.loading ?
                new Icon(Icons.hourglass_empty) :
                widget.controller.playing ?
                  new Icon(Icons.pause) :
                  new Icon(Icons.play_arrow),
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
        new Positioned(
          left: 10.0,
          bottom: 9.0,
          child: new Text(_durationToString(widget.controller.progress),
            style: new TextStyle(color: Colors.white),
          ),
        ),
        new Positioned(
          left: 35.0,
          right: 35.0,
          bottom: 0.0,
          child: new Slider(
            min: 0.0,
            max: 1.0,
            activeColor: Colors.red[900],
            value: _unitProgress,
            onChanged: (double value) => _unitSeek(value)
          ),
        ),
        new Positioned(
          right: 10.0,
          bottom: 9.0,
          child: new Text(_durationToString(widget.controller.duration),
            style: new TextStyle(color: Colors.white),
          ),
        ),
      ],
    );
  }

  @override
  Widget build(BuildContext context) {
    return new CustomSingleChildLayout(
      delegate: new _MainLayout(_layoutSize),
      child: new Stack(
        children: <Widget>[
          new GestureDetector(
            onTap: _brieflyShowControlOverlay,
            child:
              new ChildView(connection: widget.controller.videoViewConnection),
          ),
          new Offstage(
            offstage: !_shouldShowControlOverlay(),
            child: _buildControlOverlay(),
          ),
        ],
      ),
    );
  }
}

/// Layout Delegate that preserves aspect ratio if possible.
class _MainLayout extends SingleChildLayoutDelegate {
  Size _size;

  _MainLayout(this._size);

  @override
  BoxConstraints getConstraintsForChild(BoxConstraints constraints) =>
    new BoxConstraints.tight(
      constraints.constrainSizeAndAttemptToPreserveAspectRatio(_size));

  @override
  Size getSize(BoxConstraints constraints) =>
    constraints.constrainSizeAndAttemptToPreserveAspectRatio(_size);

  @override
  bool shouldRelayout(_MainLayout oldDelegate) => oldDelegate._size != _size;
}
