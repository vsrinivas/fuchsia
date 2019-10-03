// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:developer' show Timeline;

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_modular/fidl_async.dart';
import 'package:fidl_fuchsia_ui_views/fidl_async.dart' show ViewHolderToken;
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_modular/lifecycle.dart' as lifecycle;
import 'package:fuchsia_services/services.dart';
import 'package:lib.story_shell/common.dart';

import 'models/surface/surface_graph.dart';
import 'models/surface/surface_properties.dart';
import 'visual_state_watcher.dart';

/// An implementation of the [StoryShell] interface.
class StoryShellImpl extends StoryShell {
  final StoryShellBinding _storyShellBinding = StoryShellBinding();
  final StoryShellContextProxy _storyShellContext = StoryShellContextProxy();
  final KeyListener keyListener;
  final StoryVisualStateWatcherBinding _visualStateWatcherBinding =
      StoryVisualStateWatcherBinding();
  final SurfaceGraph surfaceGraph;
  final StreamController<String> _focusEventStreamController =
      StreamController.broadcast();
  String _lastFocusedSurfaceId;
  lifecycle.Lifecycle _lifecycle;
  VisualStateWatcher _storyVisualStateWatcher;

  StoryShellImpl({this.surfaceGraph, this.keyListener});

  /// StoryShell
  @override
  Future<void> initialize(
      InterfaceHandle<StoryShellContext> contextHandle) async {
    _storyVisualStateWatcher = VisualStateWatcher(
        keyListener: keyListener, storyShellContext: _storyShellContext);
    _storyShellContext.ctrl.bind(contextHandle);
    await _storyShellContext.watchVisualState(
        _visualStateWatcherBinding.wrap(_storyVisualStateWatcher));
    surfaceGraph.addListener(() {
      String surfaceId = surfaceGraph.focused?.node?.value;
      if (surfaceId != null && surfaceId != _lastFocusedSurfaceId) {
        _focusEventStreamController.add(surfaceId);
        _lastFocusedSurfaceId = surfaceId;
      }
    });
  }

  /// Bind an [InterfaceRequest] for a [StoryShell] interface to this object.
  void bindStoryShell(InterfaceRequest<StoryShell> request) {
    _storyShellBinding.bind(this, request);
  }

  /// Introduce a new Surface and corresponding [ViewHolderToken] to this Story.
  ///
  /// The Surface may have a relationship with its parent surface.
  @override
  Future<void> addSurface(
    ViewConnection viewConnection,
    SurfaceInfo surfaceInfo,
  ) async {
    Timeline.instantSync(
        'connecting surface ${viewConnection.surfaceId} with parent ${surfaceInfo.parentId}');
    log.fine(
        'Connecting surface ${viewConnection.surfaceId} with parent ${surfaceInfo.parentId}');

    /// ignore: cascade_invocations
    log.fine('Were passed manifest: $surfaceInfo.moduleManifest');
    surfaceGraph
      ..addSurface(
        viewConnection.surfaceId,
        SurfaceProperties(source: surfaceInfo.moduleSource),
        surfaceInfo.parentId,
        surfaceInfo.surfaceRelation ?? const SurfaceRelation(),
        surfaceInfo.moduleManifest != null
            ? surfaceInfo.moduleManifest.compositionPattern
            : '',
        surfaceInfo.moduleManifest != null
            ? surfaceInfo.moduleManifest.placeholderColor
            : '',
      )
      ..connectView(viewConnection.surfaceId, viewConnection.viewHolderToken);
  }

  /// DEPRECATED:  For transition purposes only.
  @override
  Future<void> addSurface2(
    ViewConnection2 viewConnection,
    SurfaceInfo surfaceInfo,
  ) async {
    return addSurface(
        ViewConnection(
            surfaceId: viewConnection.surfaceId,
            viewHolderToken: viewConnection.viewHolderToken),
        surfaceInfo);
  }

  /// Focus the surface with this id
  @override
  Future<void> focusSurface(String surfaceId) async {
    Timeline.instantSync('focusing view',
        arguments: {'surfaceId': '$surfaceId'});
    surfaceGraph.focusSurface(surfaceId);
  }

  /// Defocus the surface with this id
  @override
  Future<void> defocusSurface(String surfaceId) async {
    Timeline.instantSync('defocusing view',
        arguments: {'surfaceId': '$surfaceId'});
    surfaceGraph.dismissSurface(surfaceId);
  }

  @override
  Future<void> removeSurface(String surfaceId) async {
    return surfaceGraph.removeSurface(surfaceId);
  }

  @override
  Future<void> updateSurface(
    ViewConnection viewConnection,
    SurfaceInfo surfaceInfo,
  ) async {
    // TODO (jphsiao): implement
  }

  @override
  Stream<String> get onSurfaceFocused => _focusEventStreamController.stream;

  /// Start advertising the StoryShell service, and bind lifecycle to
  /// termination
  void advertise() {
    StartupContext.fromStartupInfo().outgoing.addPublicService(
      (InterfaceRequest<StoryShell> request) {
        Timeline.instantSync('story shell request');
        log.fine('Received binding request for StoryShell');
        bindStoryShell(request);
      },
      StoryShell.$serviceName,
    );
    _lifecycle ??= lifecycle.Lifecycle();
  }
}
