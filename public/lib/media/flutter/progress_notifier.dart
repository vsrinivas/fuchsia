// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:apps.media.lib.dart/audio_player_controller.dart';

import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';

/// Notifier for animating widgets that reflect player progress. This class
/// can be used with |AnimatedBuilder| to create widgets that animate based on
/// the progress of an |AudioPlayerController| or |MediaPlayerController|.
/// Builders should call either |withResolution| or |withExcursion|, typically
/// when setting |AnimatedBuilder.animation|. Builders that call those methods
/// should run when the controller updates. This is because |ProgressNotifier|
/// doesn't allow for transitions such as play/pause or seeking. It needs the
/// one of the 'with' methods to be called when those transitions occur.
class ProgressNotifier extends ChangeNotifier {
  AudioPlayerController _controller;
  Duration _resolution;
  Timer _timer;
  bool _disposed = false;

  /// Constructs a |ProgressNotifier|.
  ProgressNotifier(this._controller) {
    assert(_controller != null);
  }

  @override
  void dispose() {
    _disposed = true;
    _controller = null;
    _timer?.cancel();
    _timer = null;
    super.dispose();
  }

  /// Registers a one-time progress callback with the controller.
  void _register() {
    if (!_controller.playing) {
      _timer = null;
      return;
    }

    // TODO(dalesat): Take the rate into account once we support that.

    int resolutionMicroseconds = _resolution.inMicroseconds;
    int delayMicroseconds = resolutionMicroseconds -
        (_controller.progress.inMicroseconds % resolutionMicroseconds);

    _timer = new Timer(new Duration(microseconds: delayMicroseconds), () {
      _timer = null;

      if (_disposed || !_controller.playing) {
        return;
      }

      notifyListeners();
      _register();
    });
  }

  /// Sets the resolution and returns this |ProgressNotifier|. This method
  /// should be called in a builder that runs when the controller notifies of
  /// a state change.
  ProgressNotifier withResolution(Duration resolution) {
    if (_disposed) {
      return this;
    }

    if (_timer == null || _resolution != resolution) {
      _resolution = resolution;
      _timer?.cancel();
      _register();
    }

    return this;
  }

  /// Sets the excursion and returns this |ProgressNotifier|. This method
  /// should be called in a builder that runs when the controller notifies of
  /// a state change. This method is useful for sliders where |excursion| is
  /// the distance the slider moves. Use |LayoutBuilder| to get the width of
  /// the slider.
  ProgressNotifier withExcursion(double excursion, BuildContext context) {
    excursion *= MediaQuery.of(context).devicePixelRatio;
    return withResolution(new Duration(
        microseconds:
            (_controller.duration.inMicroseconds / excursion).ceil()));
  }
}
