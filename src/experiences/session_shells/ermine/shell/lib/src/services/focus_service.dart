// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:ermine/src/states/view_state.dart';
import 'package:ermine_utils/ermine_utils.dart';
import 'package:fidl_fuchsia_ui_focus/fidl_async.dart';
import 'package:fidl_fuchsia_ui_views/fidl_async.dart' hide FocusState;
import 'package:flutter/foundation.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';
import 'package:fuchsia_services/services.dart';

/// Defines a service to manage routing focus in the system.
///
/// It listens to [FocusChain] changes in the system and updates the system
/// UI. It also provides a way to programmatically request focus to any view.
class FocusService extends FocusChainListener {
  final ViewHandle hostView;
  late final ValueChanged<ViewHandle> onFocusMoved;

  final _focusChainListenerBinding = FocusChainListenerBinding();
  late final StreamSubscription<bool> _focusSubscription;

  // Holds the currently focused child view. Null, if shell has focus.
  ViewState? focusedChildView;

  // Temporary variable to guard against FocusChain overwriting the values
  // set by _onHostFocusChanged(). Note that Flatland doesn't work with
  // FocusChain.
  // TODO(fxbug.dev/93446): Remove this along with FocusChain subscription after
  // enabling Flatland by default.
  bool flatlandHasMovedFocusToChild = false;

  FocusService(ViewRef viewRef) : hostView = ViewHandle(viewRef) {
    final registryProxy = FocusChainListenerRegistryProxy();
    Incoming.fromSvcPath().connectToService(registryProxy);
    registryProxy.register(_focusChainListenerBinding.wrap(this));
    registryProxy.ctrl.close();
    _focusSubscription =
        FocusState.instance.stream().listen(_onHostFocusChanged);
  }

  void dispose() {
    _focusSubscription.cancel();
    _focusChainListenerBinding.close(0);
  }

  void _onHostFocusChanged(bool focused) {
    if (focused) {
      onFocusMoved(hostView);
      flatlandHasMovedFocusToChild = false;
    } else if (focusedChildView != null) {
      onFocusMoved(focusedChildView!.view);
      flatlandHasMovedFocusToChild = true;
    }
  }

  void setFocusOnHostView() {
    focusedChildView?.cancelSetFocus();
    focusedChildView = null;

    FocusState.instance
        .requestFocus(hostView.handle)
        .then((_) => focusedChildView = null)
        .catchError((e) {
      log.warning('Failed to request focus on host view: $e');
    });
  }

  void setFocusOnView(ViewState view) {
    if (focusedChildView != view) {
      focusedChildView?.cancelSetFocus();
    }
    focusedChildView = view..setFocus();
  }

  @override
  Future<void> onFocusChange(FocusChain focusChain) async {
    // Convert from List<ViewRef> to List<ViewHandle>
    final chain = focusChain.focusChain
        // TODO(https://fxbug.dev/71711): Figure out why `dart analyze`
        // complains about this.
        ?.map((viewRef) => ViewHandle(viewRef)) // ignore: unnecessary_lambdas
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

    if (!flatlandHasMovedFocusToChild) {
      onFocusMoved(childView);
    }
  }
}
