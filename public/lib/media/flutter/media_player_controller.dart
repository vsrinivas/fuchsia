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
import 'package:apps.media.services/net_media_player.fidl.dart';
import 'package:apps.media.services/net_media_service.fidl.dart';
import 'package:apps.media.services/problem.fidl.dart';
import 'package:apps.media.services/video_renderer.fidl.dart';
import 'package:application.lib.app.dart/app.dart';
import 'package:application.services/service_provider.fidl.dart';
import 'package:apps.mozart.lib.flutter/child_view.dart';
import 'package:apps.mozart.services.views/view_token.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';

import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';

/// Controller for MediaPlayer widgets.
class MediaPlayerController extends ChangeNotifier {
  final MediaServiceProxy _mediaService = new MediaServiceProxy();
  final NetMediaServiceProxy _netMediaService = new NetMediaServiceProxy();

  NetMediaPlayerProxy _netMediaPlayer;
  ChildViewConnection _videoViewConnection;

  // We don't mess with these except during _activate, but they need to
  // stay in scope even after _activate returns.
  AudioRendererProxy _audioRenderer;
  VideoRendererProxy _videoRenderer;

  bool _active = false;
  bool _loading = false;
  bool _playing = false;
  bool _ended = false;
  bool _hasVideo = false;

  Size _videoSize;
  TimelineFunction _timelineFunction;
  Problem _problem;
  MediaMetadata _metadata;

  bool _progressBarReady = false;
  int _progressBarMicrosecondsSinceEpoch;
  int _progressBarReferenceTime;
  int _durationNanoseconds;

  /// Constructs a MediaPlayerController.
  MediaPlayerController(ServiceProvider services) {
    connectToService(services, _mediaService.ctrl);
    connectToService(services, _netMediaService.ctrl);
    _close(); // Initialize stuff.
  }

  /// Opens a URI for playback. If there is no player or player proxy (because
  /// the controller has never been opened or has been closed), a new local
  /// player will be created. If there is a player or player proxy, the URL
  /// will be set on it.
  void open(Uri uri) {
    if (uri == null) {
      throw new ArgumentError.notNull('uri');
    }

    if (_active) {
      _netMediaPlayer.setUrl(uri.toString());
      _hasVideo = false;
      _timelineFunction = null;
    } else {
      _active = true;

      _createLocalPlayer(uri);

      _handlePlayerStatusUpdates(NetMediaPlayer.kInitialStatus, null);
    }

    notifyListeners();
  }

  /// Connects to a remote media player.
  void connectToRemote({
    @required String device,
    @required String service
  }) {
    if (device == null) {
      throw new ArgumentError.notNull('device');
    }
    if (service == null) {
      throw new ArgumentError.notNull('service');
    }

    _close();
    _active = true;

    _netMediaService.createNetMediaPlayerProxy(
      device,
      service,
      _netMediaPlayer.ctrl.request()
    );

    _handlePlayerStatusUpdates(NetMediaPlayer.kInitialStatus, null);
    notifyListeners();
  }

  /// Closes this controller, undoing a previous |open| or |connectToRemote|
  /// call. Does nothing if the controller is already closed.
  void close() {
    _close();
    notifyListeners();
  }

  @override
  void dispose() {
    close();
    super.dispose();
  }

  /// Internal version of |close|.
  void _close() {
    _active = false;

    if (_netMediaPlayer != null) {
      _netMediaPlayer.ctrl.close();
    }

    if (_audioRenderer != null) {
      _audioRenderer.ctrl.close();
    }

    if (_videoRenderer != null) {
      _videoRenderer.ctrl.close();
    }

    _netMediaPlayer = new NetMediaPlayerProxy();

    _audioRenderer = new AudioRendererProxy();
    _videoRenderer = new VideoRendererProxy();

    _playing = false;
    _ended = false;
    _loading = true;
    _hasVideo = false;

    _problem = null;
    _metadata = null;

    _progressBarReady = false;
    _durationNanoseconds = 0;
  }

  /// Creates a local player.
  void _createLocalPlayer(Uri uri) {
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

    _handleVideoRendererStatusUpdates(
      VideoRenderer.kInitialStatus,
      null
    );

    InterfacePair<mp.MediaPlayer> mediaPlayer =
      new InterfacePair<mp.MediaPlayer>();
    _mediaService.createPlayer(
      null,
      audioMediaRenderer.passHandle(),
      videoMediaRenderer.passHandle(),
      mediaPlayer.passRequest(),
    );

    _netMediaService.createNetMediaPlayer('media_player',
      mediaPlayer.passHandle(), _netMediaPlayer.ctrl.request());

    _netMediaPlayer.setUrl(uri.toString());
  }

  /// Gets the physical size of the video.
  Size get videoPhysicalSize => _hasVideo ? _videoSize : Size.zero;

  /// Gets the video view connection.
  ChildViewConnection get videoViewConnection => _videoViewConnection;

  /// Indicates whether the player has video to present.
  bool get hasVideo => _hasVideo;

  /// Indicates whether the player is in the process of loading content.
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

    return new Duration(
      microseconds: _progressNanoseconds.clamp(0, _durationNanoseconds) ~/ 1000
    );
  }

  int get _progressNanoseconds {
    // Estimate FrameInfo::presentationTime.
    if (_timelineFunction == null) {
      return 0;
    }

    int microseconds = (new DateTime.now()).microsecondsSinceEpoch -
        _progressBarMicrosecondsSinceEpoch;
    int referenceNanoseconds = microseconds * 1000 + _progressBarReferenceTime;
    return _timelineFunction(referenceNanoseconds);
  }

  /// Starts or resumes playback.
  void play() {
    if (!_active || _playing) {
      return;
    }

    if (_ended) {
      _netMediaPlayer.seek(0);
    }

    _netMediaPlayer.play();
  }

  /// Pauses playback.
  void pause() {
    if (!_active || !_playing) {
      return;
    }

    _netMediaPlayer.pause();
  }

  /// Seeks to a position expressed as a Duration.
  void seek(Duration position) {
    if (!_active) {
      return;
    }

    int positionNanoseconds =
        (position.inMicroseconds * 1000).round().clamp(0, _durationNanoseconds);

    _netMediaPlayer.seek(positionNanoseconds);

    if (!_playing) {
      play();
    }
  }

  // Handles a status update from the player and requests a new update. Call
  // with kInitialStatus, null to initiate status updates.
  void _handlePlayerStatusUpdates(int version, mp.MediaPlayerStatus status) {
    if (!_active) {
      return;
    }

    if (status != null) {
      if (status.timelineTransform != null) {
        _timelineFunction =
            new TimelineFunction.fromTransform(status.timelineTransform);
      }

      _hasVideo = status.contentHasVideo;
      _ended = status.endOfStream;
      _playing = !ended &&
          _timelineFunction != null &&
          _timelineFunction.subjectDelta != 0;

      _problem = status.problem;
      _metadata = status.metadata;

      if (_metadata != null) {
        _loading = false;
        _durationNanoseconds = _metadata.duration;
      }

      if (_progressBarReady && _progressNanoseconds < 0) {
        // We thought the progress bar was ready, but we're getting negative
        // progress values. That means our assumption about reference time
        // correlation is probably wrong. We need to prepare the progress bar
        // again. See the comment in |_prepareProgressBar|.
        // TODO(dalesat): Remove once we're given access to presentation time.
        // https://fuchsia.atlassian.net/browse/US-130
        _progressBarReady = false;
      }

      if (_timelineFunction != null && _timelineFunction.referenceTime != 0 &&
        !_progressBarReady) {
        _prepareProgressBar();
      }

      scheduleMicrotask(() {
        notifyListeners();
      });
    }

    _netMediaPlayer.getStatus(version, _handlePlayerStatusUpdates);
  }

  // Handles a status update from the video renderer and requests a new update.
  // Call with kInitialStatus, null to initiate status updates.
  void _handleVideoRendererStatusUpdates(
    int version,
    VideoRendererStatus status
  ) {
    if (!_active) {
      return;
    }

    if (status != null) {
      _videoSize = new Size(
        status.videoSize.width.toDouble(),
        status.videoSize.height.toDouble()
      );

      scheduleMicrotask(() {
        notifyListeners();
      });
    }

    _videoRenderer.getStatus(version, _handleVideoRendererStatusUpdates);
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
    // One instance in which our correlation assumption falls down is when
    // we're connecting to a (remote) player whose current timeline was
    // established some time ago. In this case, the reference time in the
    // timeline function correlates to a past time, and the progress values we
    // get will be negative. When that happens, this function should be called
    // again.
    _progressBarMicrosecondsSinceEpoch =
        (new DateTime.now()).microsecondsSinceEpoch;
    _progressBarReferenceTime = _timelineFunction.referenceTime;
    _progressBarReady = true;
  }
}
