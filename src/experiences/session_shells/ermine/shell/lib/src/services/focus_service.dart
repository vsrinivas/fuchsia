// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:ermine_utils/ermine_utils.dart';
import 'package:fidl_fuchsia_ui_focus/fidl_async.dart';
import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:flutter/foundation.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart';

/// Defines a service to manage routing focus in the system.
///
/// It listens to [FocusChain] changes in the system and updates the system
/// UI. It also provides a way to programmatically request focus to any view.
class FocusService extends FocusChainListener {
  final ViewHandle hostView;
  late final ValueChanged<ViewHandle> onFocusMoved;

  final _focusChainListenerBinding = FocusChainListenerBinding();

  // Holds a list of all child views mapped to their focused descendant view.
  final _views = <ViewHandle, ViewHandle>{};

  FocusService(ViewRef viewRef) : hostView = ViewHandle(viewRef) {
    final registryProxy = FocusChainListenerRegistryProxy();
    Incoming.fromSvcPath().connectToService(registryProxy);
    registryProxy.register(_focusChainListenerBinding.wrap(this));
    registryProxy.ctrl.close();
  }

  void dispose() {
    _focusChainListenerBinding.close(0);
  }

  void removeView(ViewHandle view) {
    _views.remove(view);
  }

  void moveFocus(ViewHandle view) {
    (_views[view] ?? view).focus();
  }

  @override
  Future<void> onFocusChange(FocusChain focusChain) async {
    // Convert from List<ViewRef> to List<ViewHandle>
    final chain = focusChain.focusChain
        ?.map((viewRef) => ViewHandle(viewRef))
        .toList(growable: false);

    // Focus chain for the shell will be of the format:
    // [RootView, ..., ShellView, ChildView, [..., FocusedView]]
    // The last view is the view that has actual input focus.
    // Parse it to extract [ChildView, DescendantView]
    if (chain == null || !chain.contains(hostView)) {
      log.severe('WARNING: Focus chain does not include shell view koid');
      return;
    }

    // The shell has temporary focus if it is last in the chain.
    if (chain.last == hostView) {
      onFocusMoved(hostView);
      return;
    }

    // The child view should immediately follow shell view.
    final index = chain.lastIndexOf(hostView);
    final childView = chain[index + 1];

    // The focused view is a descendant of child view or child view itself.
    _views[childView] = chain.last;
    onFocusMoved(childView);
  }
}
