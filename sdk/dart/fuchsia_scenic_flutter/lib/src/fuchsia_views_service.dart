// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui' as ui;

import 'package:flutter/services.dart';
import 'package:meta/meta.dart';
import 'package:flutter/gestures.dart';
import 'package:fidl_fuchsia_ui_pointerinjector/fidl_async.dart';

/// Defines a singleton [PlatformViewChannel] used to create and control
/// Fuchsia specific platform views.
class FuchsiaViewsService {
  // FuchsiaViewsService is a singleton because there is only ever one
  // entry-point into the native code for a given platform channel.
  static final FuchsiaViewsService instance = FuchsiaViewsService._();

  // The platform view channel used to communicate with flutter engine.
  final _platformViewChannel = MethodChannel(
    'flutter/platform_views',
    JSONMethodCodec(),
  );

  /// The [MethodChannel] used to communicate with Flutter Embedder.
  @internal
  MethodChannel get platformViewChannel => _platformViewChannel;

  /// Holds the method call handlers registered by the view id.
  final _callHandlers = <int, Future<dynamic> Function(MethodCall call)?>{};

  // Private constructor. Registers a method call handler with the platform
  // view.
  FuchsiaViewsService._() {
    platformViewChannel.setMethodCallHandler((call) async {
      // Guard against invalid or missing arguments.
      try {
        // Call the method call handler registered for viewId.
        int? viewId = (call.arguments as Map)['viewId'];
        return _callHandlers[viewId]?.call(call);
        // ignore: avoid_catches_without_on_clauses
      } catch (e) {
        // If viewId is missing, call the last registered handler.
        return _callHandlers.values.last?.call(call);
      }
    });
  }

  /// Register a [MethodCall] handler for a given [viewId].
  void register(
          int viewId, Future<dynamic> Function(MethodCall call)? handler) =>
      _callHandlers[viewId] = handler;

  /// Deregister existing [MethodCall] handler for a given [viewId].
  void deregister(int viewId) => _callHandlers.remove(viewId);

  /// Creates a platform view with [viewId] and given properties.
  Future<void> createView(
    int viewId, {
    bool hitTestable = true,
    bool focusable = true,
    Rect viewOcclusionHint = Rect.zero,
  }) async {
    final Map<String, dynamic> args = <String, dynamic>{
      'viewId': viewId,
      'hitTestable': hitTestable,
      'focusable': focusable,
      'viewOcclusionHintLTRB': <double>[
        viewOcclusionHint.left,
        viewOcclusionHint.top,
        viewOcclusionHint.right,
        viewOcclusionHint.bottom
      ],
    };
    return platformViewChannel.invokeMethod('View.create', args);
  }

  /// Updates view properties of the platform view associated with [viewId].
  Future<void> updateView(
    int viewId, {
    bool hitTestable = true,
    bool focusable = true,
    Rect viewOcclusionHint = Rect.zero,
  }) async {
    final Map<String, dynamic> args = <String, dynamic>{
      'viewId': viewId,
      'hitTestable': hitTestable,
      'focusable': focusable,
      'viewOcclusionHintLTRB': <double>[
        viewOcclusionHint.left,
        viewOcclusionHint.top,
        viewOcclusionHint.right,
        viewOcclusionHint.bottom
      ],
    };
    return platformViewChannel.invokeMethod('View.update', args);
  }

  /// Destroys the platform view associated with [viewId].
  Future<void> destroyView(int viewId) async {
    final Map<String, dynamic> args = <String, dynamic>{
      'viewId': viewId,
    };
    return platformViewChannel.invokeMethod('View.dispose', args);
  }

  /// Dispatch [PointerEvent] event to [viewId].
  Future<void> dispatchPointerEvent(
      {required int viewId,
      required PointerEvent pointer,
      int? viewRef}) async {
    if (!_hasFuchsiaEventPhase(pointer)) {
      return;
    }

    // We use the global position in the parent's local coordinate system.
    // The injection viewport is set up to coincide with the local coordinate
    // system, and all child-specific transforms are handled on the server.
    final x = pointer.position.dx;
    final y = pointer.position.dy;

    // Use the logical space of the parent view as the injection viewport.
    // It means that pointer coordinates, in the parent's view, can be used
    // verbatim for injecting into a child view. The fuchsia.ui.pointerinjector
    // server handles the pointer coordinate transforms for the child view.
    //
    // Note that the Flutter instance's logical space can change size, but since
    // the logical space *always* has its origin at Offset.zero, a size change
    // does not need a new viewport, since the viewport merely anchors pointer
    // coordinates received by the Flutter instance.
    ui.Size window = ui.window.physicalSize / ui.window.devicePixelRatio;

    final phase = pointer is PointerDownEvent
        ? EventPhase.add
        : pointer is PointerUpEvent
            ? EventPhase.remove
            : pointer is PointerMoveEvent
                ? EventPhase.change
                : EventPhase.cancel;

    final args = _makeInjectArgs(
        viewId: viewId,
        x: x,
        y: y,
        phase: _getEventPhaseValue(phase),
        pointerId: ((pointer.device << 32) >> 32),
        // `traceFlowId` and `pointer.pointer` are program specific nonces.
        traceFlowId: pointer.pointer,
        logicalWidth: window.width,
        logicalHeight: window.height,
        timestamp: pointer.timeStamp.inMicroseconds * 1000,
        viewRef: viewRef);

    return platformViewChannel.invokeMethod(
        'View.pointerinjector.inject', args);
  }

  Map<String, dynamic> _makeInjectArgs(
      {required int viewId,
      required double x,
      required double y,
      required int phase,
      required int pointerId,
      required int traceFlowId,
      required double logicalWidth,
      required double logicalHeight,
      required int timestamp,
      int? viewRef}) {
    final args = <String, dynamic>{
      'viewId': viewId,
      'x': x,
      'y': y,
      'phase': phase,
      'pointerId': pointerId,
      'traceFlowId': traceFlowId,
      'logicalWidth': logicalWidth,
      'logicalHeight': logicalHeight,
      'timestamp': timestamp
    };

    // In Flatland, the Flutter engine holds the ViewRef and does not provide it
    // to the dart code, so we do not include it in the platform message.
    if (viewRef != null) {
      args['viewRef'] = viewRef;
    }
    return args;
  }

  // Check if [PointerEvent] is one of supported events.
  bool _hasFuchsiaEventPhase(PointerEvent? pointer) {
    // TODO(fxbug.dev/84030) - Implement stream consistency checking: inject
    // only valid streams, and reject broken streams.
    return pointer != null &&
        (pointer is PointerDownEvent ||
            pointer is PointerUpEvent ||
            pointer is PointerMoveEvent ||
            pointer is PointerCancelEvent);
  }

  int _getEventPhaseValue(EventPhase phase) {
    switch (phase) {
      case EventPhase.add:
        return 1;
      case EventPhase.change:
        return 2;
      case EventPhase.remove:
        return 3;
      case EventPhase.cancel:
        return 4;
      default:
        return 0;
    }
  }
}
