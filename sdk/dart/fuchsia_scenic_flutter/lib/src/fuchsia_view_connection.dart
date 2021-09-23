// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: avoid_as, null_check_always_fails, unnecessary_null_comparison

import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:flutter/widgets.dart';
import 'package:fuchsia_scenic/views.dart';
import 'package:meta/meta.dart';
import 'package:zircon/zircon.dart';

import 'fuchsia_view_controller.dart';
import 'pointer_injector.dart';

/// Defines a thin wrapper around [FuchsiaViewController].
///
/// Its primary purpose is to hold on to [ViewHolderToken] for the lifetime of
/// the view controller, since [FuchsiaViewController] is agnostic to all
/// Fuchsia data types. (Eventually, [FuchsiaView] and [FuchsiaViewController]
/// will be moved to Flutter framework, which cannot have Fuchsia data types.)
class FuchsiaViewConnection extends FuchsiaViewController {
  /// The Gfx view tree token when the view is attached.
  final ViewHolderToken? viewHolderToken;

  /// The Flatland token when the view is attached.
  final ViewportCreationToken? viewportCreationToken;

  /// The handle to the view used for [requestFocus] calls.
  final ViewRef? viewRef;

  PointerInjector? _pointerInjector;

  /// Returns the [PointerInjector] instance used by this connection.
  @visibleForTesting
  PointerInjector get pointerInjector => _pointerInjector ??=
      PointerInjector.fromSvcPath(onError: onPointerInjectionError);

  /// Callback when the connection to child's view is connected to view tree.
  final FuchsiaViewConnectionCallback? _onViewConnected;

  /// Callback when the child's view is disconnected from view tree.
  final FuchsiaViewConnectionCallback? _onViewDisconnected;

  /// Callback when the child's view render state changes.
  final FuchsiaViewConnectionStateCallback? _onViewStateChanged;

  /// Set to true if pointer injection into child views should be enabled.
  /// This requires the view's [ViewRef] to be set during construction.
  final bool usePointerInjection;

  /// Constructor.
  FuchsiaViewConnection(
    this.viewHolderToken, {
    this.viewRef,
    FuchsiaViewConnectionCallback? onViewConnected,
    FuchsiaViewConnectionCallback? onViewDisconnected,
    FuchsiaViewConnectionStateCallback? onViewStateChanged,
    this.usePointerInjection = false,
  })  : assert(viewHolderToken!.value != null && viewHolderToken.value.isValid),
        assert(
            viewRef?.reference == null || viewRef!.reference.handle!.isValid),
        assert(!usePointerInjection || viewRef?.reference != null),
        viewportCreationToken = null,
        _onViewConnected = onViewConnected,
        _onViewDisconnected = onViewDisconnected,
        _onViewStateChanged = onViewStateChanged,
        super(
          viewId: viewHolderToken!.value.handle!.handle,
          onViewConnected: _handleViewConnected,
          onViewDisconnected: _handleViewDisconnected,
          onViewStateChanged: _handleViewStateChanged,
          onPointerEvent: _handlePointerEvent,
        );

  FuchsiaViewConnection.flatland(
    this.viewportCreationToken, {
    this.viewRef,
    FuchsiaViewConnectionCallback? onViewConnected,
    FuchsiaViewConnectionCallback? onViewDisconnected,
    FuchsiaViewConnectionStateCallback? onViewStateChanged,
    this.usePointerInjection = false,
  })  : assert(viewportCreationToken!.value != null &&
            viewportCreationToken.value.isValid),
        assert(
            viewRef?.reference == null || viewRef!.reference.handle!.isValid),
        assert(!usePointerInjection || viewRef?.reference != null),
        viewHolderToken = null,
        _onViewConnected = onViewConnected,
        _onViewDisconnected = onViewDisconnected,
        _onViewStateChanged = onViewStateChanged,
        super(
          viewId: viewportCreationToken!.value.handle!.handle,
          onViewConnected: _handleViewConnected,
          onViewDisconnected: _handleViewDisconnected,
          onViewStateChanged: _handleViewStateChanged,
          onPointerEvent: _handlePointerEvent,
        );

  /// Requests that focus be transferred to the remote Scene represented by
  /// this connection.
  @override
  Future<void> requestFocus([int _ = 0]) async {
    assert(viewRef?.reference != null && _ == 0);
    return super.requestFocus(viewRef!.reference.handle!.handle);
  }

  static void _handleViewStateChanged(
      FuchsiaViewController controller, bool? state) async {
    FuchsiaViewConnection connection = controller as FuchsiaViewConnection;
    connection._onViewStateChanged?.call(controller, state);
  }

  @visibleForTesting
  ViewRef get hostViewRef => ScenicContext.hostViewRef();

  static void _handleViewConnected(FuchsiaViewController controller) async {
    FuchsiaViewConnection connection = controller as FuchsiaViewConnection;
    connection._onViewConnected?.call(controller);
  }

  static void _handleViewDisconnected(FuchsiaViewController controller) {
    FuchsiaViewConnection connection = controller as FuchsiaViewConnection;
    connection._onViewDisconnected?.call(controller);
    // TODO(fxbug.dev/84035): Dispose eagerly, on widget disconnect lifecycle
    // event. Here, injection races with view-disconnect on the server.
    if (connection.usePointerInjection) {
      connection.pointerInjector.dispose();
    }
  }

  static Future<void> _handlePointerEvent(
      FuchsiaViewController controller, PointerEvent pointer) async {
    FuchsiaViewConnection connection = controller as FuchsiaViewConnection;
    if (!connection.usePointerInjection) {
      return;
    }

    // If we haven't made a pointerInjector for this View yet, or if the old one
    // was torn down, we need to create one.
    if (!connection.pointerInjector.registered) {
      // The only valid pointer event to start an injector with is DOWN event.
      // This check affords protection against the first stream if malformed,
      // but does not guard against future malformed streams.
      if (!(pointer is PointerDownEvent)) {
        return;
      }

      // Create the pointerInjector.
      final viewRefDup = ViewRef(
          reference:
              connection.viewRef!.reference.duplicate(ZX.RIGHT_SAME_RIGHTS));

      connection.pointerInjector.register(
        hostViewRef: connection.hostViewRef,
        viewRef: viewRefDup,
      );
    }

    return connection.pointerInjector.dispatchEvent(pointer: pointer);
  }

  @visibleForTesting
  void onPointerInjectionError() {
    // Dispose the current instance of pointer injector. It gets recreated on
    // next _handlePointerEvent().
    pointerInjector.dispose();
    _pointerInjector = null;
  }
}
