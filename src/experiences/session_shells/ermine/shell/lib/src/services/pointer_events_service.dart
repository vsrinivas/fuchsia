// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:ui';

import 'package:fidl_fuchsia_ui_input/fidl_async.dart';
import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:flutter/painting.dart';
import 'package:fuchsia_services/services.dart';

/// The screen edge where the peek is detected.
enum PeekEdge { left, right, top, bottom }

// Defines the states of the peek state machine.
enum _PeekState { none, onEdge, peeking }

/// Defines a service to listen to out-of-band PointerEvents delivered directly
/// from the [PointerCaptureListener] API.
///
/// It is used to implement 'peek' functionality for auto-hiding the UI at the
/// edge of screen. When the user hovers the mouse at the edge of the
/// screen and holds it there for a brief moment (300 ms), the [onPeekBegin]
/// callback is invoked. This can be used to show the peek UI. If then, the user
/// moves the mouse away from the edge + specified inset, [onPeekEnd] is fired,
/// which can be used to hide the peek UI.
class PointerEventsService extends PointerCaptureListener {
  late final void Function(PeekEdge) onPeekBegin;
  late final VoidCallback onPeekEnd;
  late final VoidCallback onActivity;

  /// The insets at the edges where the peek state is true.
  final EdgeInsets insets;

  /// Flag to control when to start/stop listening to pointer events.
  bool listen = false;

  final _binding = PointerCaptureListenerBinding();
  final _registry = PointerCaptureListenerRegistryProxy();

  PointerEventsService(
    ViewRef viewRef, {
    required this.insets,
  }) {
    Incoming.fromSvcPath().connectToService(_registry);
    _registry.registerListener(_binding.wrap(this), viewRef);
  }

  void dispose() {
    _binding.close();
    _registry.ctrl.close();
  }

  // The timer used to detect how long the pointer stayed at the peek edge.
  Timer? _timer;

  // The state of the peek state machine.
  var _state = _PeekState.none;
  PeekEdge? _edge;

  @override
  Future<void> onPointerEvent(PointerEvent event) async {
    // Report pointer event to activity tracking service.
    // TODO(http://fxb/80131): Remove once activity is reported in the input
    // pipeline.
    onActivity();

    if (!listen || event.phase != PointerEventPhase.move) {
      return;
    }

    final screenSize = window.physicalSize / window.devicePixelRatio;

    // Check if the pointer is at the edge of the screen.
    final edge = _atEdge(screenSize, event);
    final atEdge = edge != null;

    switch (_state) {
      case _PeekState.none:
        if (atEdge) {
          _state = _PeekState.onEdge;
          _edge = edge;
          _timer = Timer(Duration(milliseconds: 300), () {
            // If pointer position is still at edge, we are peeking!.
            _state = _PeekState.peeking;
            // Although edge can't be null here, without the assertion Dart
            // won't compile.
            onPeekBegin(edge!); // ignore: unnecessary_non_null_assertion
          });
        }
        break;
      case _PeekState.onEdge:
        // If pointer is NOT at edge, cancel the timer.
        if (!atEdge) {
          _timer?.cancel();
          _state = _PeekState.none;
          _edge = null;
        }
        break;
      case _PeekState.peeking:
        // Check if the pointer has left the peek area (edge inset).
        final notAtEdge = _notAtEdge(screenSize, event);
        // Exit peeking state if pointer moves out of the inset area.
        if (notAtEdge) {
          _state = _PeekState.none;
          _edge = null;
          onPeekEnd();
        }
        break;
    }
  }

  PeekEdge? _atEdge(Size screenSize, PointerEvent event) {
    if (insets.bottom > 0) {
      if (screenSize.height.floor() - event.y.floor() <= 1) {
        return PeekEdge.bottom;
      }
    }
    if (insets.right > 0) {
      if (screenSize.width.floor() - event.x.floor() <= 1) {
        return PeekEdge.right;
      }
    }
    if (insets.top > 0) {
      if (event.y.floor() <= 1) {
        return PeekEdge.top;
      }
    }
    if (insets.left > 0) {
      if (event.x.floor() <= 1) {
        return PeekEdge.left;
      }
    }
    return null;
  }

  bool _notAtEdge(Size screenSize, PointerEvent event) {
    if (_edge == PeekEdge.bottom &&
        screenSize.height - event.y.floor() > insets.bottom) {
      return true;
    } else if (_edge == PeekEdge.right &&
        screenSize.width - event.x.floor() > insets.right) {
      return true;
    } else if (_edge == PeekEdge.top && event.y.floor() > insets.top) {
      return true;
    } else if (_edge == PeekEdge.left && event.x.floor() > insets.left) {
      return true;
    }
    return false;
  }
}
