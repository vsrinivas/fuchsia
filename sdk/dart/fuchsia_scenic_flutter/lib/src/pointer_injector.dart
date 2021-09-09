// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: avoid_as, unnecessary_null_comparison
// ignore_for_file: avoid_catches_without_on_clauses

import 'dart:core';
import 'dart:math';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:fidl_fuchsia_ui_pointerinjector/fidl_async.dart';
import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:flutter/gestures.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart';
import 'package:meta/meta.dart';
import 'package:pedantic/pedantic.dart';

/// Defines a class that uses the pointer injector service to inject pointer
/// events into child views.
///
/// Requires following services in the environment:
///    fuchsia.ui.pointerinjector.Registry
class PointerInjector {
  final Registry _registry;
  final DeviceProxy _device;

  /// Callback when the injector encounters error during registration or when
  /// injecting pointer events. Client code should call [dispose] and release
  /// this instance. It may create another instance to continue injecting. Using
  /// this instance after [onError] will result in undefined behavior.
  final ui.VoidCallback onError;

  /// Returns [true] if the PointerInjector is successfully registered.
  bool registered = false;

  /// Store events for injecting into the fuchsia.ui.pointerinjector channel;
  /// the flow control policy allows at most one call in flight at a time.
  final List<Event> _buffer = [];

  /// Semaphore to control injection call on fuchsia.ui.pointerinjector channel;
  /// the flow control policy allows at most one call in flight at a time.
  bool _injectInFlight = false;

  /// Constructor used for injecting mocks during testing.
  @visibleForTesting
  PointerInjector(
    Registry registry,
    DeviceProxy device, {
    required this.onError,
  })  : _registry = registry,
        _device = device;

  /// Construct PointerInjector from [/svc].
  factory PointerInjector.fromSvcPath({required ui.VoidCallback onError}) {
    final registry = RegistryProxy();
    final device = DeviceProxy();
    Incoming.fromSvcPath().connectToService(registry);
    return PointerInjector(registry, device, onError: onError);
  }

  /// Closes connections to services.
  void dispose() {
    if (_registry is RegistryProxy) {
      RegistryProxy proxy = _registry as RegistryProxy;
      proxy.ctrl.close();
    }
    _device.ctrl.close();
  }

  /// Registers with the pointer injector service.
  ///
  /// Returns immediately after submitting the registration request.
  /// This means that the registration request may still be in-flight
  /// on the server side when this function returns. Events can safely be
  /// injected into the channel while registration is pending ("feed forward").
  void register({
    required ViewRef hostViewRef,
    required ViewRef viewRef,
  }) {
    final config = Config(
      deviceId: 1,
      deviceType: DeviceType.touch,
      context: Context.withView(hostViewRef),
      target: Target.withView(viewRef),
      viewport: Viewport(
        extents: _viewportExtents(),
        viewportToContextTransform: _identityTransform(),
      ),
      dispatchPolicy: DispatchPolicy.exclusiveTarget,
    );

    final future =
        _registry.register(config, _device.ctrl.request()).catchError((e) {
      log.warning('Failed to register pointer injector: $e.');
      registered = false;
      // Registration failures are critical; higher-level code should handle
      // them.
      onError();
    });
    registered = true;
    unawaited(future);
  }

  /// Dispatch [PointerEvent] event to embedded child.
  Future<void> dispatchEvent({
    required PointerEvent pointer,
  }) async {
    if (!_isValidPointerEvent(pointer)) {
      return;
    }

    // We use the global position in the parent's local coordinate system.
    // The injection viewport is set up to coincide with the local coordinate
    // system, and all child-specific transforms are handled on the server.
    final x = pointer.position.dx;
    final y = pointer.position.dy;
    final phase = pointer is PointerDownEvent
        ? EventPhase.add
        : pointer is PointerUpEvent
            ? EventPhase.remove
            : pointer is PointerMoveEvent
                ? EventPhase.change
                : EventPhase.cancel;

    final sample = PointerSample(
      pointerId: pointer.device,
      phase: phase,
      positionInViewport: Float32List.fromList([x, y]),
    );
    final injectorEvent = Event(
      timestamp: pointer.timeStamp.inMicroseconds * 1000,
      data: Data.withPointerSample(sample),
      traceFlowId: pointer.pointer, // TODO(fxbug.dev/84032)
    );

    // Queue up the event, because an injection call may be in flight.
    _buffer.add(injectorEvent);

    if (_injectInFlight) {
      // Reachable if a previous call is stuck on the await, below. The _buffer
      // ensures this call's event gets injected, eventually, in FIFO order.
      return;
    }

    _injectInFlight = true;
    while (_buffer.isNotEmpty) {
      // FIDL call has a maximum size; fit in the limit.
      int bufferEnd = min(_buffer.length, maxInject);
      List<Event> batch = _buffer.sublist(0, bufferEnd);
      _buffer.removeRange(0, bufferEnd);
      await _device.inject(batch).catchError((e) {
        log.warning('Failed to dispatch pointer events: $e.');
        onError();
      });
      // After await, this call may wake up to find more events queued up in
      // _buffer. Keep draining _buffer until all events are injected.
    }
    _injectInFlight = false;
  }

  // Check if [PointerEvent] is one of supported events.
  bool _isValidPointerEvent(PointerEvent pointer) {
    // TODO(fxbug.dev/84030) - Implement stream consistency checking: inject
    // only valid streams, and reject broken streams.
    return pointer != null &&
        (pointer is PointerDownEvent ||
            pointer is PointerUpEvent ||
            pointer is PointerMoveEvent);
  }

  static List<Float32List> _viewportExtents() {
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
    return [
      Float32List.fromList([0, 0]),
      Float32List.fromList([window.width, window.height]),
    ];
  }

  static Float32List _identityTransform() => Float32List.fromList(<double>[
        1, 0, 0, // first column
        0, 1, 0, // second column
        0, 0, 1, // third column
      ]);
}
