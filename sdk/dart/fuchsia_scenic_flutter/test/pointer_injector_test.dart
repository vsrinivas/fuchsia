// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: avoid_as, dead_code, null_check_always_fails

import 'dart:async';
import 'dart:ui';

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_ui_pointerinjector/fidl_async.dart';
import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fuchsia_logger/logger.dart';
// ignore: implementation_imports
import 'package:fuchsia_scenic_flutter/src/pointer_injector.dart';
import 'package:mockito/mockito.dart';
import 'package:pedantic/pedantic.dart';
import 'package:zircon/zircon.dart';

void main() {
  setupLogger();

  test('PointerInjector register', () async {
    final device = _mockDevice();
    final registry = _mockRegistry();
    final injector = PointerInjector(registry, device, onError: () {});

    final hostViewRef = _mockViewRef();
    final viewRef = _mockViewRef();
    injector.register(hostViewRef: hostViewRef, viewRef: viewRef);

    verify(registry.register(captureAny, any)).captured.single;
    expect(injector.registered, isTrue);
  });

  test('PointerInjector dispatchEvent', () async {
    MockDevice device = _mockDevice() as MockDevice;
    final registry = _mockRegistry();
    final injector = PointerInjector(registry, device, onError: () {});
    when(device.inject(any)).thenAnswer((_) => Future<void>.value());

    final pointer = PointerDownEvent(position: Offset(10, 10));
    await injector.dispatchEvent(pointer: pointer);

    final List<Event> events =
        verify(device.inject(captureAny)).captured.single;
    expect(events.length, 1);
    final pointerEvent = events.first;
    expect(pointerEvent.data!.pointerSample!.phase, EventPhase.add);
    expect(pointerEvent.data!.pointerSample!.positionInViewport, [10.0, 10.0]);
  });

  test('PointerInjector dispatchEvent logs exception and onError', () async {
    MockDevice device = _mockDevice() as MockDevice;
    final registry = _mockRegistry();
    bool error = false;
    final injector =
        PointerInjector(registry, device, onError: () => error = true);
    when(device.inject(any)).thenAnswer((_) =>
        Future<void>.error(FidlStateException('Proxy<Device> is closed.')));

    // Check for errors being thrown or logs being generated.
    bool logsError = false;
    log.onRecord.listen((event) {
      logsError = true;
    });

    final pointer = PointerDownEvent(position: Offset(10, 10));
    await injector.dispatchEvent(
      pointer: pointer,
    );
    expect(logsError, isTrue);
    expect(error, isTrue);
  });

  test('PointerInjector dispatchEvent is correctly reentrant', () async {
    MockDevice device = _mockDevice() as MockDevice;
    final registry = _mockRegistry();
    final injector = PointerInjector(registry, device, onError: () {});

    // Control the wakeup time of each device.inject FIDL call, independently.
    // There are three 'dispatchEvent' Dart calls, but two 'device.inject' FIDL
    // calls (because flow control).
    final Completer<void> injectCallFinished_1 = Completer<void>();
    final Completer<void> injectCallFinished_2 = Completer<void>();
    final answers = [injectCallFinished_1, injectCallFinished_2];
    when(device.inject(any)).thenAnswer((_) => answers.removeAt(0).future);

    // Make the Dart calls.
    final down = PointerDownEvent(position: Offset(10, 10));
    unawaited(injector.dispatchEvent(
      pointer: down,
    ));
    final move = PointerMoveEvent(position: Offset(15, 15));
    unawaited(injector.dispatchEvent(
      pointer: move,
    ));
    final up = PointerUpEvent(position: Offset(20, 20));
    unawaited(injector.dispatchEvent(
      pointer: up,
    ));

    // Allow the first 'dispatchEvent' call to continue draining the buffer.
    injectCallFinished_1.complete(); // resume loop
    // Allow the first 'dispatchEvent' call to exit the drain loop.
    injectCallFinished_2.complete(); // resume loop
    // Sequence the verification step to *after* the loop finishes completely.
    await untilCalled(device.inject(any));

    var verification = verify(device.inject(captureAny));
    expect(verification.captured.length, 2); // two FIDL injections
    expect(verification.captured[0].length, 1); // first batch, just the add
    expect(
        verification.captured[1].length, 2); // second batch, change and remove

    final downInjected = verification.captured[0].single;
    expect(downInjected.data!.pointerSample!.phase, EventPhase.add);
    expect(downInjected.data!.pointerSample!.positionInViewport, [10.0, 10.0]);

    final moveInjected = verification.captured[1][0];
    expect(moveInjected.data!.pointerSample!.phase, EventPhase.change);
    expect(moveInjected.data!.pointerSample!.positionInViewport, [15.0, 15.0]);

    final upInjected = verification.captured[1][1];
    expect(upInjected.data!.pointerSample!.phase, EventPhase.remove);
    expect(upInjected.data!.pointerSample!.positionInViewport, [20.0, 20.0]);
  });
}

ViewRef _mockViewRef() {
  final viewRef = MockViewRef();
  final eventPair = MockEventPair();
  when(viewRef.reference).thenReturn(eventPair);
  when(eventPair.duplicate(any)).thenReturn(eventPair);
  when(eventPair.isValid).thenReturn(true);

  return viewRef;
}

MockRegistry _mockRegistry() {
  final registry = MockRegistry();
  when(registry.register(any, any)).thenAnswer((_) => Future<void>.value());
  return registry;
}

DeviceProxy _mockDevice() {
  final device = MockDevice();
  final ctrl = MockCtrl();
  when(device.ctrl).thenReturn(ctrl);
  return device;
}

class MockCtrl extends Mock implements AsyncProxyController<Device> {}

class MockDevice extends Mock implements DeviceProxy {
  @override
  Future<void> inject(List<Event>? events) =>
      super.noSuchMethod(Invocation.method(#inject, [events]));
}

class MockRegistry extends Mock implements Registry {
  @override
  Future<void> register(Config? config, InterfaceRequest<Device>? injector) =>
      super.noSuchMethod(Invocation.method(#register, [config, injector]));
}

class MockViewRef extends Mock implements ViewRef {
  @override
  int get hashCode => super.noSuchMethod(Invocation.method(#hashCode, []));

  @override
  bool operator ==(dynamic other) =>
      super.noSuchMethod(Invocation.method(#==, [other]));
}

class MockEventPair extends Mock implements EventPair {
  @override
  EventPair duplicate(int? options) =>
      super.noSuchMethod(Invocation.method(#duplicate, [options]));
}
