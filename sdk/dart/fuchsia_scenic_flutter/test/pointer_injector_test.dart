// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: avoid_as, dead_code, null_check_always_fails

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
import 'package:zircon/zircon.dart';

void main() {
  setupLogger();

  test('PointerInjector register', () async {
    final device = _mockDevice();
    final registry = _mockRegistry();
    final injector = PointerInjector(registry, device, onError: () {});

    final hostViewRef = _mockViewRef();
    final viewRef = _mockViewRef();
    final rect = Rect.fromLTWH(0, 0, 100, 100);
    injector.register(
        hostViewRef: hostViewRef, viewRef: viewRef, viewport: rect);

    final config = verify(registry.register(captureAny, any)).captured.single;
    expect(
        config.viewport.extents,
        equals([
          [0.0, 0.0],
          [100.0, 100.0]
        ]));
    expect(config.viewport.viewportToContextTransform,
        equals([1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0]));
    expect(injector.registered, isTrue);
  });

  test('PointerInjector dispatchEvent', () async {
    MockDevice device = _mockDevice() as MockDevice;
    final registry = _mockRegistry();
    final injector = PointerInjector(registry, device, onError: () {});
    when(device.inject(any)).thenAnswer((_) => Future<void>.value());

    final rect = Rect.fromLTWH(0, 0, 100, 100);
    final pointer = PointerDownEvent(position: Offset(10, 10));
    await injector.dispatchEvent(pointer: pointer, viewport: rect);

    final List<Event> events =
        verify(device.inject(captureAny)).captured.single;
    expect(events.length, 2);
    final viewportEvent = events.first;
    expect(
        viewportEvent.data!.viewport!.extents,
        equals([
          [0.0, 0.0],
          [100.0, 100.0]
        ]));
    final pointerEvent = events.last;
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

    final rect = Rect.fromLTWH(0, 0, 100, 100);
    final pointer = PointerDownEvent(position: Offset(10, 10));
    await injector.dispatchEvent(pointer: pointer, viewport: rect);
    expect(logsError, isTrue);
    expect(error, isTrue);
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
