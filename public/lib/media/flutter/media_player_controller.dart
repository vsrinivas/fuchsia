// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:apps.media.lib.dart/audio_player_controller.dart';
import 'package:apps.media.services/media_renderer.fidl.dart';
import 'package:apps.media.services/media_service.fidl.dart';
import 'package:apps.media.services/video_renderer.fidl.dart';
import 'package:application.lib.app.dart/app.dart';
import 'package:application.services/service_provider.fidl.dart';
import 'package:apps.mozart.lib.flutter/child_view.dart';
import 'package:lib.fidl.dart/bindings.dart';

import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';

/// Controller for MediaPlayer widgets.
class MediaPlayerController
  extends AudioPlayerController
  implements Listenable {
  final MediaServiceProxy _mediaService = new MediaServiceProxy();

  final List<VoidCallback> _listeners = new List<VoidCallback>();

  ChildViewConnection _videoViewConnection;

  // We don't mess with this except during _activate, but it needs to stay in
  // scope even after _activate returns.
  VideoRendererProxy _videoRenderer;

  Size _videoSize = Size.zero;

  bool _disposed = false;

  /// Constructs a MediaPlayerController.
  MediaPlayerController(ServiceProvider services) : super(services) {
    updateCallback = _notifyListeners;
    connectToService(services, _mediaService.ctrl);
    _close(); // Initialize stuff.
  }

  @override
  void open(Uri uri, {String serviceName = 'media_player'}) {
    bool wasActive = openOrConnected;
    super.open(uri, serviceName: serviceName);

    if (!wasActive) {
      InterfacePair<ViewOwner> viewOwnerPair = new InterfacePair<ViewOwner>();
      _videoRenderer.createView(viewOwnerPair.passRequest());

      _videoViewConnection =
        new ChildViewConnection(viewOwnerPair.passHandle());

      _handleVideoRendererStatusUpdates(VideoRenderer.kInitialStatus, null);
    }

    _notifyListeners();
  }

  @override
  void connectToRemote({ String device, String service }) {
    _close();
    super.connectToRemote(device: device, service: service);
  }

  @override
  void close() {
    _close();
    super.close();
    _notifyListeners();
  }

  void _close() {
    if (_videoRenderer != null) {
      _videoRenderer.ctrl.close();
    }

    _videoRenderer = new VideoRendererProxy();
  }

  @override
  void addListener(VoidCallback listener) {
    if (_disposed) {
      throw new StateError('Object disposed');
    }

    _listeners.add(listener);
  }

  @override
  void removeListener(VoidCallback listener) {
    if (_disposed) {
      throw new StateError('Object disposed');
    }

    _listeners.remove(listener);
  }

  void _notifyListeners() {
    _listeners.forEach((VoidCallback l) => l());
  }

  /// Discards any resources used by the object. After this is called, the
  /// object is not in a usable state and should be discarded (calls to
  /// addListener and removeListener will throw after the object is disposed).
  @mustCallSuper
  void dispose() {
    _disposed = true;
    close();
    _listeners.clear();
  }

  @override
  InterfacePair<MediaRenderer> get videoMediaRenderer {
    InterfacePair<MediaRenderer> videoMediaRenderer =
      new InterfacePair<MediaRenderer>();

    _mediaService.createVideoRenderer(
      _videoRenderer.ctrl.request(),
      videoMediaRenderer.passRequest(),
    );

    return videoMediaRenderer;
  }

  /// Gets the physical size of the video.
  Size get videoPhysicalSize => hasVideo ? _videoSize : Size.zero;

  /// Gets the video view connection.
  ChildViewConnection get videoViewConnection => _videoViewConnection;

  // Handles a status update from the video renderer and requests a new update.
  // Call with kInitialStatus, null to initiate status updates.
  void _handleVideoRendererStatusUpdates(
    int version,
    VideoRendererStatus status
  ) {
    if (!openOrConnected) {
      return;
    }

    if (status != null) {
      double pixelAspectRatio =
        status.pixelAspectRatio.width.toDouble() /
        status.pixelAspectRatio.height.toDouble();

      _videoSize = new Size(
        status.videoSize.width.toDouble() * pixelAspectRatio,
        status.videoSize.height.toDouble()
      );

      scheduleMicrotask(() {
        _notifyListeners();
      });
    }

    _videoRenderer.getStatus(version, _handleVideoRendererStatusUpdates);
  }
}
