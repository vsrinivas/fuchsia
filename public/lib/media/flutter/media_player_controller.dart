// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:apps.media.lib.flutter/timeline.dart';
import 'package:apps.media.services/audio_renderer.fidl.dart';
import 'package:apps.media.services/media_metadata.fidl.dart';
import 'package:apps.media.services/media_player.fidl.dart' as mp;
import 'package:apps.media.services/media_renderer.fidl.dart';
import 'package:apps.media.services/media_service.fidl.dart';
import 'package:apps.media.services/problem.fidl.dart';
import 'package:apps.media.services/seeking_reader.fidl.dart';
import 'package:apps.media.services/video_renderer.fidl.dart';
import 'package:apps.modular.lib.app.dart/app.dart';
import 'package:apps.modular.services.application/service_provider.fidl.dart';
import 'package:apps.mozart.lib.flutter/child_view.dart';
import 'package:apps.mozart.services.geometry/geometry.fidl.dart' as fidl;
import 'package:apps.mozart.services.views/view_token.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';

import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';

/// Controller for MediaPlayer widgets.
class MediaPlayerController extends ChangeNotifier {
  final MediaServiceProxy _mediaService = new MediaServiceProxy();

  Uri _uri;

  mp.MediaPlayerProxy _mediaPlayer;
  ChildViewConnection _videoViewConnection;

  // We don't mess with these except during _activate, but they need to
  // stay in scope even after _activate returns.
  AudioRendererProxy _audioRenderer;
  VideoRendererProxy _videoRenderer;
  InterfacePair<SeekingReader> _reader;

  bool _active;
  bool _loading;
  bool _playing;
  bool _ended;
  bool _audioOnly;

  Size _videoSize;
  TimelineFunction _timelineFunction;
  Problem _problem;
  MediaMetadata _metadata;

  bool _progressBarReady;
  int _progressBarMicrosecondsSinceEpoch;
  int _progressBarReferenceTime;
  int _durationNanoseconds;

  /// Constructs a MediaPlayerController.
  MediaPlayerController(ServiceProvider services) {
    connectToService(services, _mediaService.ctrl);
  }

  /// Gets the current URI.
  Uri get uri => _uri;

  /// Sets the current URI.
  set uri(Uri value) {
    if (_uri == value) {
      return;
    }

    _uri = value;

    if (_uri == null) {
      // We were playing a URI. Clear the player to shut that down.
      _deactivate();
    } else {
      // Need to play the new URI.
      _activate();
    }

    notifyListeners();
  }

  /// Gets the physical size of the video.
  Size get videoPhysicalSize => _videoSize;

  /// Gets the video view connection.
  ChildViewConnection get videoViewConnection => _videoViewConnection;

  /// Indicates whether the current content is audio-only
  bool get audioOnly => _audioOnly;

  /// Indicates whether the current content is audio-only
  bool get loading => _loading;

  /// Indicates whether the player is currently playing.
  bool get playing => _playing;

  /// Indicates whether the player is at end-of-stream.
  bool get ended => _ended;

  /// Gets the current problem, if there is one. If this value is non-null,
  /// some issue is preventing playback, and this value describes what that
  /// issue is.
  Problem get problem => _problem;

  /// Gets the current content metadata, if any.
  MediaMetadata get metadata => _metadata;

  /// Gets the duration of the content.
  Duration get duration =>
    new Duration(microseconds: _durationNanoseconds ~/ 1000);

  /// Gets current playback progress.
  Duration get progress {
    if (!_progressBarReady) {
      return Duration.ZERO;
    }

    // Estimate FrameInfo::presentationTime.
    int microseconds = (new DateTime.now()).microsecondsSinceEpoch -
      _progressBarMicrosecondsSinceEpoch;
    int referenceNanoseconds = microseconds * 1000 + _progressBarReferenceTime;
    int progressNanoseconds =
      _timelineFunction(referenceNanoseconds).clamp(0, _durationNanoseconds);

    return new Duration(microseconds: progressNanoseconds ~/ 1000);
  }

  /// Starts or resumes playback.
  void play() {
    if (!_active || _playing) {
      return;
    }

    if (_ended) {
      _mediaPlayer.seek(0);
    }

    _mediaPlayer.play();
  }

  /// Pauses playback.
  void pause() {
    if (!_active || !_playing) {
      return;
    }

    _mediaPlayer.pause();
  }

  /// Seeks to a position expressed as a Duration.
  void seek(Duration position) {
    if (!_active) {
      return;
    }

    int positionNanoseconds =
      (position.inMicroseconds * 1000).round().clamp(0, _durationNanoseconds);

    _mediaPlayer.seek(positionNanoseconds);

    if (!_playing) {
      play();
    }
  }

  @override
  void dispose() {
    _deactivate();
    super.dispose();
  }

  /// Transitions to inactive state, releasing any underlying resources.
  void _deactivate() {
    _active = false;

    _mediaPlayer = new mp.MediaPlayerProxy();

    _audioRenderer = new AudioRendererProxy();
    _videoRenderer = new VideoRendererProxy();
    _reader = new InterfacePair<SeekingReader>();

    _videoSize = Size.zero;

    _playing = false;
    _ended = false;
    _loading = true;
    _audioOnly = true;

    _problem = null;
    _metadata = null;

    _progressBarReady = false;
    _durationNanoseconds = 0;
  }

  /// Transitions to active state, constructing the required underlying
  /// resources. [_uri] must be non-null when this method is called.
  void _activate() {
    assert(_uri != null);

    _deactivate();

    _active = true;

    InterfacePair<MediaRenderer> audioMediaRenderer =
      new InterfacePair<MediaRenderer>();
    _mediaService.createAudioRenderer(
      _audioRenderer.ctrl.request(),
      audioMediaRenderer.passRequest(),
    );

    InterfacePair<MediaRenderer> videoMediaRenderer =
      new InterfacePair<MediaRenderer>();
    _mediaService.createVideoRenderer(
      _videoRenderer.ctrl.request(),
      videoMediaRenderer.passRequest(),
    );

    InterfacePair<ViewOwner> viewOwnerPair = new InterfacePair<ViewOwner>();
    _videoRenderer.createView(viewOwnerPair.passRequest());

    _videoViewConnection =
      new ChildViewConnection(viewOwnerPair.passHandle());

    _videoRenderer.getVideoSize((fidl.Size size) {
      _videoSize = new Size(size.width.toDouble(), size.height.toDouble());
      _audioOnly = false;
      notifyListeners();
    });

    if (_uri.scheme == 'file') {
      _mediaService.createFileReader(_uri.toFilePath(), _reader.passRequest());
    } else {
      _mediaService.createNetworkReader(_uri.toString(), _reader.passRequest());
    }

    _mediaService.createPlayer(
      _reader.passHandle(),
      audioMediaRenderer.passHandle(),
      videoMediaRenderer.passHandle(),
      _mediaPlayer.ctrl.request()
    );

    _handleStatusUpdates(mp.MediaPlayer.kInitialStatus, null);
  }

  // Handles a status update from the player and requests a new update. Call
  // with kInitialStatus, null to initiate status updates.
  void _handleStatusUpdates(int version, mp.MediaPlayerStatus status) {
    if (!_active) {
      return;
    }

    if (status != null) {
      if (status.timelineTransform != null) {
        _timelineFunction =
          new TimelineFunction.fromTransform(status.timelineTransform);
      }

      _ended = status.endOfStream;
      _playing = !ended && _timelineFunction != null &&
        _timelineFunction.subjectDelta != 0;

      _problem = status.problem;
      _metadata = status.metadata;

      if (_metadata != null) {
        _loading = false;
        _durationNanoseconds = _metadata.duration;
      }

      if (_timelineFunction != null && _timelineFunction.referenceTime != 0 &&
        !_progressBarReady) {
        _prepareProgressBar();
      }

      scheduleMicrotask(() {
        notifyListeners();
      });
    }

    _mediaPlayer.getStatus(version, _handleStatusUpdates);
  }

  /// Captures information required to implement the progress bar.
  void _prepareProgressBar() {
    // Capture the correlation between the system clock and the reference time
    // from the timeline function, which we assume to be roughly 'now' in the
    // FrameInfo::presentationTime sense. This is a rough approximation and
    // could break for any number of reasons. We currently have to do this
    // because flutter doesn't provide access to FrameInfo::presentationTime.
    // TODO(dalesat): Fix once we're given access to presentation time.
    // https://fuchsia.atlassian.net/browse/US-130
    _progressBarMicrosecondsSinceEpoch =
      (new DateTime.now()).microsecondsSinceEpoch;
    _progressBarReferenceTime = _timelineFunction.referenceTime;
    _progressBarReady = true;
  }
}
