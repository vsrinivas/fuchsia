// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_sys/fidl_async.dart';
import 'package:fidl_fuchsia_ui_app/fidl_async.dart';
import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:fuchsia_scenic_flutter/child_view_connection.dart';
import 'package:fuchsia_services/services.dart';
import 'package:zircon/zircon.dart';

import '../utils/suggestion.dart';

/// Defines a class to represent a story in ermine.
class ErmineStory {
  final Suggestion suggestion;
  final ValueChanged<ErmineStory> onDelete;
  final ValueChanged<ErmineStory> onChange;
  final LauncherProxy _launcher;
  final ComponentControllerProxy _componentController;

  ErmineStory({
    this.suggestion,
    Launcher launcher,
    this.onDelete,
    this.onChange,
    ComponentControllerProxy componentController,
  })  : _launcher = launcher,
        _componentController =
            componentController ?? ComponentControllerProxy(),
        nameNotifier = ValueNotifier(suggestion.title),
        childViewConnectionNotifier = ValueNotifier(null) {
    launchSuggestion();
    _componentController.onTerminated.listen((_) => delete());
  }

  String get id => suggestion.id;

  final ValueNotifier<String> nameNotifier;
  String get name => nameNotifier.value ?? id;
  set name(String value) => nameNotifier.value = value;

  ValueNotifier<bool> focusedNotifier = ValueNotifier(false);
  bool get focused => focusedNotifier.value;
  set focused(bool value) => focusedNotifier.value = value;

  final ValueNotifier<ChildViewConnection> childViewConnectionNotifier;
  ChildViewConnection get childViewConnection =>
      childViewConnectionNotifier.value;

  ValueNotifier<bool> fullscreenNotifier = ValueNotifier(false);
  bool get fullscreen => fullscreenNotifier.value;
  set fullscreen(bool value) => fullscreenNotifier.value = value;
  bool get isImmersive => fullscreenNotifier.value == true;

  void delete() {
    _componentController.kill();
    _componentController.ctrl.close();
    onDelete?.call(this);
  }

  void focus() => onChange?.call(this..focused = true);

  void maximize() => onChange?.call(this..fullscreen = true);

  void restore() => onChange?.call(this..fullscreen = false);

  ValueNotifier<bool> editStateNotifier = ValueNotifier(false);
  void edit() => editStateNotifier.value = !editStateNotifier.value;

  @visibleForTesting
  Future<void> launchSuggestion() async {
    final incoming = Incoming();

    await _launcher.createComponent(
      LaunchInfo(
        url: suggestion.url,
        directoryRequest: incoming.request().passChannel(),
      ),
      _componentController.ctrl.request(),
    );

    ViewProviderProxy viewProvider = ViewProviderProxy();
    incoming.connectToService(viewProvider);
    await incoming.close();

    final viewTokens = EventPairPair();
    assert(viewTokens.status == ZX.OK);
    final viewHolderToken = ViewHolderToken(value: viewTokens.first);
    final viewToken = ViewToken(value: viewTokens.second);

    await viewProvider.createView(viewToken.value, null, null);
    viewProvider.ctrl.close();

    childViewConnectionNotifier.value = ChildViewConnection(
      viewHolderToken,
      onAvailable: (_) {},
      onUnavailable: (_) {},
    );
  }
}
