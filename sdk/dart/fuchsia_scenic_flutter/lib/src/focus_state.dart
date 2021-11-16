// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'fuchsia_views_service.dart';

class FocusState {
  // FocusState should be singleton, since we need to ensure that
  // HostView.getNextFocusState method calls are not duplicated.
  static final FocusState instance = FocusState._();

  // Private constructor.
  FocusState._();

  // Make sure _focusStateChanges does not buffer, since we already prepend the
  // current focus state to stream().
  final Stream<bool> _focusStateChanges =
      _watchFocusState().asBroadcastStream();

  /// Gets the current focus state of the root viewRef.
  Future<bool> isFocused() async =>
      await FuchsiaViewsService.instance.platformViewChannel
          .invokeMethod('View.focus.getCurrent') as bool;

  /// Listen for focus state events on the host viewRef. When listening, users
  /// will get the current focus state, followed by any future focus states.
  /// The returned stream instance should be cancelled whenever users are done
  /// listening to prevent memory leaks.
  ///
  /// See //sdk/fidl/fuchsia.ui.views/view_ref_focused.fidl for additional
  /// documentation on what certain focus state transitions mean.
  Stream<bool> stream() {
    late final StreamController<bool> controller;
    controller = StreamController<bool>(
      // ignore: unnecessary_lambdas
      onCancel: () => controller.close(),
    );
    isFocused().then((state) => controller
      ..add(state)
      ..addStream(_focusStateChanges));
    return controller.stream;
  }

  // Async stream generator that recursively calls HostView.getNextFocusState.
  // We need to make sure that these calls are not duplicated.
  static Stream<bool> _watchFocusState() async* {
    yield await FuchsiaViewsService.instance.platformViewChannel
        .invokeMethod('View.focus.getNext') as bool;
    yield* _watchFocusState();
  }

  /// Requests that focus be transferred to the remote Scene represented by
  /// this connection.
  Future<void> requestFocus(int viewRef) async {
    final args = <String, dynamic>{
      'viewRef': viewRef,
    };
    final result = await FuchsiaViewsService.instance.platformViewChannel
        .invokeMethod('View.focus.request', args);
    // Throw OSError if result is non-zero.
    if (result != 0) {
      throw OSError(
        'Failed to request focus for view: $viewRef with $result',
        result,
      );
    }
  }

  /// Requests that focus be transferred to the remote Scene by ViewId
  /// This method is indended for use by FuchsiaViewConnection only.
  Future<void> requestFocusById(int viewId) async {
    final args = <String, dynamic>{
      'viewId': viewId,
    };

    final result = await FuchsiaViewsService.instance.platformViewChannel
        .invokeMethod('View.focus.requestById', args);
    // Throw OSError if result is non-zero. This may be because the ViewId is
    // invalid (i.e. the view has not been created or destroyed yet). If you are
    // trying to set focus on a FuchsiaViewConnection immediately after creating
    // it and it is failing you may be racing against scenic providing a view ref
    // back to the flutter engine, and it may be effective to wait a moment and retry.
    if (result != 0) {
      throw OSError(
        'Failed to request focus for view: $viewId with $result',
        result,
      );
    }
  }
}
