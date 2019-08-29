// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:developer' show Timeline;
import 'dart:typed_data';

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_mem/fidl_async.dart' as fuchsia_mem;
import 'package:fidl_fuchsia_modular/fidl_async.dart';
import 'package:fidl_fuchsia_ui_views/fidl_async.dart' show ViewHolderToken;
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_modular/lifecycle.dart' as lifecycle;
import 'package:fuchsia_services/services.dart';
import 'package:lib.story_shell/common.dart';
import 'package:zircon/zircon.dart';

import 'models/surface/surface_graph.dart';
import 'models/surface/surface_properties.dart';
import 'visual_state_watcher.dart';

/// An implementation of the [StoryShell] interface.
class StoryShellImpl extends StoryShell {
  final StoryShellBinding _storyShellBinding = StoryShellBinding();
  final StoryShellContextProxy _storyShellContext = StoryShellContextProxy();
  final LinkProxy _linkProxy = LinkProxy();
  final KeyListener keyListener;
  final StoryVisualStateWatcherBinding _visualStateWatcherBinding =
      StoryVisualStateWatcherBinding();
  final SurfaceGraph surfaceGraph;
  final String _storyShellLinkName = 'story_shell_state';
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
    await _storyShellContext
        .getLink(_linkProxy.ctrl.request())
        .then((v) => reloadStoryState())
        .then((onLinkContentsFetched));
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
    return persistStoryState();
  }

  /// Defocus the surface with this id
  @override
  Future<void> defocusSurface(String surfaceId) async {
    Timeline.instantSync('defocusing view',
        arguments: {'surfaceId': '$surfaceId'});
    surfaceGraph.dismissSurface(surfaceId);
    return persistStoryState();
  }

  @Deprecated('Deprecated')
  @override
  Future<void> addContainer(
    String containerName,
    String parentId,
    SurfaceRelation relation,
    List<ContainerLayout> layouts,
    List<ContainerRelationEntry> relationships,
    List<ContainerView> views,
  ) async {
    // Add a root node for the container
    Timeline.instantSync('adding container', arguments: {
      'containerName': '$containerName',
      'parentId': '$parentId'
    });
    surfaceGraph.addContainer(
      containerName,
      SurfaceProperties(),
      parentId,
      relation,
      layouts,
    );
    Map<String, ContainerRelationEntry> nodeMap =
        <String, ContainerRelationEntry>{};
    Map<String, List<String>> parentChildrenMap = <String, List<String>>{};
    Map<String, ViewHolderToken> viewMap = <String, ViewHolderToken>{};
    for (ContainerView view in views) {
      viewMap[view.nodeName] = view.viewHolderToken;
    }
    for (ContainerRelationEntry relatedNode in relationships) {
      nodeMap[relatedNode.nodeName] = relatedNode;
      parentChildrenMap
          .putIfAbsent(relatedNode.parentNodeName, () => <String>[])
          .add(relatedNode.nodeName);
    }
    List<String> nodeQueue =
        views.map((ContainerView v) => v.nodeName).toList();
    List<String> addedParents = <String>[containerName];
    int i = 0;
    while (nodeQueue.isNotEmpty) {
      String nodeId = nodeQueue.elementAt(i);
      String parentId = nodeMap[nodeId].parentNodeName;
      if (addedParents.contains(parentId)) {
        for (nodeId in parentChildrenMap[parentId]) {
          SurfaceProperties prop = SurfaceProperties()
            ..containerMembership = <String>[containerName]
            ..containerLabel = nodeId;
          surfaceGraph.addSurface(
              nodeId, prop, parentId, nodeMap[nodeId].relationship, null, '');
          addedParents.add(nodeId);
          surfaceGraph.connectView(nodeId, viewMap[nodeId]);
          nodeQueue.remove(nodeId);
          surfaceGraph.focusSurface(nodeId);
        }
        i = 0;
      } else {
        i++;
        if (i > nodeQueue.length) {
          log.warning('''Error iterating through container children.
          All nodes iterated without finding all parents specified in
          Container Relations''');
          return;
        }
      }
    }
  }

  @override
  Future<void> removeSurface(String surfaceId) async {
    return surfaceGraph.removeSurface(surfaceId);
  }

  @override
  Future<void> reconnectView(ViewConnection viewConnection) async {
    // TODO (jphsiao): implement
  }

  @override
  Future<void> updateSurface(
    ViewConnection viewConnection,
    SurfaceInfo surfaceInfo,
  ) async {
    // TODO (jphsiao): implement
  }

  Future<fuchsia_mem.Buffer> reloadStoryState() async {
    return _linkProxy.get([_storyShellLinkName]);
  }

  void onLinkContentsFetched(fuchsia_mem.Buffer buffer) {
    var dataVmo = SizedVmo(buffer.vmo.handle, buffer.size);
    var data = dataVmo.read(buffer.size);
    dataVmo.close();
    dynamic decoded = jsonDecode(utf8.decode(data.bytesAsUint8List()));
    if (decoded is Map<String, dynamic>) {
      surfaceGraph.reload(decoded.cast<String, dynamic>());
    }
  }

  Future<void> persistStoryState() async {
    String encoded = json.encode(surfaceGraph);
    var jsonList = Uint8List.fromList(utf8.encode(encoded));
    var data = fuchsia_mem.Buffer(
      vmo: SizedVmo.fromUint8List(jsonList),
      size: jsonList.length,
    );
    await _linkProxy.set([_storyShellLinkName], data);
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
