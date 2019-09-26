// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:collection';
import 'dart:convert' show utf8;
import 'package:flutter/foundation.dart';
import 'package:fidl_fuchsia_app_discover/fidl_async.dart'
    show StoryDiscoverContext, SurfaceData;

import 'package:fidl_fuchsia_modular/fidl_async.dart'
    show
        ModuleData,
        StoryInfo,
        StoryController,
        StoryState,
        StoryVisibilityState;
import 'package:fidl_fuchsia_mem/fidl_async.dart' as fidl_mem;
import 'package:fuchsia_modular_flutter/session_shell.dart'
    show SessionShell, Story;
import 'package:fuchsia_modular_flutter/story_shell.dart'
    show StoryShell, StoryShellTransitional, Surface;
import 'package:fuchsia_scenic_flutter/child_view_connection.dart';
import 'package:story_shell_labs_lib/layout/deja_layout.dart';
import 'package:zircon/zircon.dart';

import 'cluster_model.dart';

/// Defines a concrete implementation for [Story] for Ermine.
class ErmineStory implements Story, StoryShell, StoryShellTransitional {
  @override
  final StoryInfo info;

  final SessionShell sessionShell;

  final StoryController controller;

  final ClustersModel clustersModel;

  final DejaLayout layoutManager = DejaLayout();

  ErmineStory({
    this.info,
    this.sessionShell,
    this.controller,
    this.clustersModel,
  });

  @override
  String get id => info.id;

  ValueNotifier<String> nameNotifier = ValueNotifier(null);
  String get name => nameNotifier.value ?? id;
  set name(String value) => nameNotifier.value = value;

  ValueNotifier<ChildViewConnection> childViewConnectionNotifier =
      ValueNotifier(null);

  @override
  ChildViewConnection get childViewConnection =>
      childViewConnectionNotifier.value;

  @override
  set childViewConnection(ChildViewConnection value) =>
      childViewConnectionNotifier.value = value;

  ValueNotifier<bool> focusedNotifier = ValueNotifier(false);
  @override
  bool get focused => focusedNotifier.value;

  @override
  set focused(bool value) => focusedNotifier.value = value;

  ValueNotifier<StoryState> stateNotifier = ValueNotifier(null);
  @override
  StoryState get state => stateNotifier.value;

  @override
  set state(StoryState value) => stateNotifier.value = value;

  ValueNotifier<StoryVisibilityState> visibilityStateNotifier =
      ValueNotifier(null);

  @override
  StoryVisibilityState get visibilityState => visibilityStateNotifier.value;

  @override
  set visibilityState(StoryVisibilityState value) {
    // We don't use modular's handling of immersive state because there is no
    // way to set it using it's apis. Instead, we use the [fullscreen]
    // accessors below to control visibility state.
  }

  bool get fullscreen => visibilityState == StoryVisibilityState.immersive;

  set fullscreen(bool value) => visibilityStateNotifier.value =
      value ? StoryVisibilityState.immersive : StoryVisibilityState.default$;

  bool get isImmersive => visibilityState == StoryVisibilityState.immersive;

  @override
  void delete() => sessionShell.deleteStory(id);

  @override
  void focus() => sessionShell.focusStory(id);

  @override
  void stop() => sessionShell.stopStory(id);

  void maximize() {
    clustersModel.maximize(id);
  }

  void restore() {
    clustersModel.restore(id);
  }

  bool get useInProcessStoryShell => clustersModel.useInProcessStoryShell;
  // Holds the [ModuleData] mapped by its path.
  final _modules = <String, ModuleData>{};

  /// Callback when a module is added.
  @override
  void onModuleAdded(ModuleData moduleData) {
    _modules[moduleData.modulePath.last] = moduleData;
  }

  /// Callback when a module is focused.
  @override
  void onModuleFocused(List<String> modulePath) {}
  ValueNotifier<bool> editStateNotifier = ValueNotifier(false);
  void edit() => editStateNotifier.value = !editStateNotifier.value;

  final _surfaces = <Surface>[];

  @override
  void onSurfaceAdded(Surface surface) {
    if (discoverContext != null) {
      discoverContext.getSurfaceData(surface.id).then((surfaceData) {
        _addSurface(surface, surfaceData);
      });
    } else {
      _addSurface(surface, null);
    }
  }

  void _addSurface(Surface surface, SurfaceData surfaceData) {
    _surfaces.add(surface);
    final parameterTypes = surfaceData?.parameterTypes ?? [];
    layoutManager.addSurface(
      intent: surfaceData?.action ?? 'NONE',
      parameters: UnmodifiableListView<String>(parameterTypes),
      surfaceId: surface.id,
      view: surface.childViewConnection,
    );
  }

  @override
  void onSurfaceFocusChange(Surface surface, {bool focus = false}) {
    // Lookup the module from the surface id.
    final surfaceId = _sanitizeSurfaceId(surface.id.replaceAll('\\', ''));
    if (_modules.containsKey(surfaceId)) {
      name = _extractPackageName(_modules[surfaceId].moduleUrl);
    }
  }

  @override
  void onSurfaceRemoved(Surface surface) {
    _surfaces.remove(surface);
    _modules.remove(_sanitizeSurfaceId(surface.id));
    layoutManager.removeSurface([surface.id]);
  }

  @override
  // TODO: implement surfaces
  Iterable<Surface> get surfaces => _surfaces;

  @override
  StoryDiscoverContext discoverContext;

  set title(String value) {
    final title = value.trim();
    if (discoverContext != null && title.isNotEmpty && title != info.id) {
      final data = utf8.encode(title);
      discoverContext.setProperty(
          'title',
          fidl_mem.Buffer(
            vmo: SizedVmo.fromUint8List(data),
            size: data.length,
          ));
    }
  }
}

String _sanitizeSurfaceId(String surfaceId) => surfaceId.replaceAll('\\', '');
String _extractPackageName(String packageUrl) {
  // Try and parse package Urls that look like:
  // fuchsia-pkg://fuchsia.com/<package-name>#meta/<component-name>.cmx
  final uri = Uri.tryParse(packageUrl);
  if (uri == null) {
    return packageUrl;
  }
  // If path (/<package-name>) is contained in fragment (meta/<component-name)
  // like in: fuchsia-pkg://fuchsia.com/settings#meta/settings.cmx
  // then return the bare package name.
  if (uri.fragment.contains(uri.path)) {
    // skip the '/' at the start.
    if (uri.path[0] == '/') {
      return uri.path.substring(1);
    } else {
      return uri.path;
    }
  }
  return packageUrl;
}
