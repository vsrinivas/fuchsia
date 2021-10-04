// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: avoid_as, unnecessary_null_comparison

import 'dart:async';

import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';

import 'focus_state.dart';
import 'fuchsia_views_service.dart';

/// Defines a callback to receive view connected and disconnected event.
typedef FuchsiaViewConnectionCallback = void Function(
    FuchsiaViewController connection);

/// Defines a callback to receive view state changed event.
typedef FuchsiaViewConnectionStateCallback = void Function(
    FuchsiaViewController connection, bool? newState);

/// Defines a callback to receive pointer events for the embedded view.
typedef FuchsiaPointerEventsCallback = Future<void> Function(
    FuchsiaViewController, PointerEvent);

/// A connection to a fuchsia view.  It can be used to construct a [FuchsiaView]
/// widget that will display the view's contents on their own layer.
class FuchsiaViewController implements PlatformViewController {
  /// The raw value of the [ViewHolderToken] or [ViewportCreationToken] where
  /// this view is attached.
  @override
  final int viewId;

  /// Callback when the connection to child's view is connected to view tree.
  final FuchsiaViewConnectionCallback? onViewConnected;

  /// Callback when the child's view is disconnected from view tree.
  final FuchsiaViewConnectionCallback? onViewDisconnected;

  /// Callback when the child view's state changes.
  final FuchsiaViewConnectionStateCallback? onViewStateChanged;

  /// Callback when pointer events are dispatched on top of child view.
  final FuchsiaPointerEventsCallback? onPointerEvent;

  // The [bool] that tracks whether the platform view was created or not yet.
  // The [FuchsiaViewController] will only create its associated platform view
  // once during the controller's lifetime.
  bool _viewCreated = false;

  // The [Completer] that is completed when the platform view is connected.
  Completer _whenConnected = Completer();

  /// The future that completes when the platform view is connected.
  Future get whenConnected => _whenConnected.future;

  /// Returns true when platform view is connected.
  bool get connected => _whenConnected.isCompleted;

  /// Constructor.
  FuchsiaViewController({
    required this.viewId,
    this.onViewConnected,
    this.onViewDisconnected,
    this.onViewStateChanged,
    this.onPointerEvent,
  }) : assert(viewId != null);

  /// Connects to the platform view given it's [viewId].
  ///
  /// Called by [FuchsiaView] when the platform view is ready to be initialized
  /// and should not be called directly. This MUST NOT be called directly.
  Future<void> connect({
    bool hitTestable = true,
    bool focusable = true,
    Rect viewOcclusionHint = Rect.zero,
  }) async {
    if (_viewCreated) return;
    _viewCreated = true; // Only allow this to be called once

    // Setup callbacks for receiving view events.
    FuchsiaViewsService.instance.register(viewId, (call) async {
      switch (call.method) {
        case 'View.viewConnected':
          _whenConnected.complete();
          onViewConnected?.call(this);
          break;
        case 'View.viewDisconnected':
          _whenConnected = Completer();
          onViewDisconnected?.call(this);
          break;
        case 'View.viewStateChanged':
          final state =
              call.arguments['state'] == 1 || call.arguments['state'] == true;
          onViewStateChanged?.call(this, state);
          break;
        default:
          print('Unknown method call from platform view channel: $call');
      }
    });

    // Now send a create message to the platform view.
    return FuchsiaViewsService.instance.createView(
      viewId,
      hitTestable: hitTestable,
      focusable: focusable,
      viewOcclusionHint: viewOcclusionHint,
    );
  }

  /// Updates properties on the platform view given it's [viewId].
  ///
  /// Called by [FuchsiaView] when the [focusable] or [hitTestable] or
  /// [viewOcclusionHint] properties are changed.  This MUST NOT be called
  /// directly.
  Future<void> update({
    bool focusable = true,
    bool hitTestable = true,
    Rect viewOcclusionHint = Rect.zero,
  }) async {
    return FuchsiaViewsService.instance.updateView(
      viewId,
      hitTestable: hitTestable,
      focusable: focusable,
      viewOcclusionHint: viewOcclusionHint,
    );
  }

  /// Requests that focus be transferred to the remote Scene represented by
  /// this connection.
  Future<void> requestFocus(int viewRef) async {
    return FocusState.instance.requestFocus(viewRef);
  }

  /// Dispose relevant resources when the view is take [OffStage] by Flutter.
  ///
  /// There are currently no resources that need to be released when the
  /// [PlatformView] is taken [Offstage] because the underlying Fuchsia view
  /// owner is still alive and the view can be bought back on stage.
  @override
  Future<void> dispose() async {}

  @override
  Future<void> clearFocus() async {}

  /// Dispatch pointer events for the child view. This MUST NOT be called
  /// directly.
  @override
  Future<void> dispatchPointerEvent(PointerEvent event) async {
    return onPointerEvent?.call(this, event);
  }

  /// Returns the viewport rect of the child view in parent's coordinates.
  ///
  /// This only works if there is one and only one widget that has the [Key]
  /// set to [GlobalObjectKey] using this constroller's instance. [FuchsiaView]
  /// set's its [key] to the controller instance passed to it.
  ///
  /// Returns [null] if the child view has not been rendered.
  ///
  /// Note: this viewport is *not* used by the injector, not needed there.
  Rect? get viewport {
    final key = GlobalObjectKey(this);
    RenderBox? box = key.currentContext?.findRenderObject() as RenderBox?;
    if (box?.hasSize == true) {
      final offset = box!.localToGlobal(Offset.zero);
      return offset & box.size;
    }
    return null;
  }
}
