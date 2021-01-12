// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui';

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_ui_pointerinjector/fidl_async.dart';
import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mockito/mockito.dart';
import 'package:zircon/zircon.dart';

// ignore: implementation_imports
import 'package:fuchsia_scenic_flutter/src/pointer_injector.dart';

void main() {
  test('PointerInjector register', () async {
    final device = _mockDevice();
    final registry = MockRegistry();
    final viewRefInstalled = MockViewRefInstalled();
    final injector = PointerInjector(registry, viewRefInstalled, device);

    final hostViewRef = _mockViewRef();
    final viewRef = _mockViewRef();
    final rect = Rect.fromLTWH(0, 0, 100, 100);
    await injector.register(
        hostViewRef: hostViewRef, viewRef: viewRef, viewport: rect);

    verify(viewRefInstalled.watch(any!));
    final config = verify(registry.register(captureAny!, any!)).captured.single;
    expect(
        config.viewport.extents,
        equals([
          [0.0, 0.0],
          [100.0, 100.0]
        ]));
    expect(config.viewport.viewportToContextTransform,
        equals([1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0]));
  });

  test('PointerInjector dispatchEvent', () async {
    final device = _mockDevice();
    final registry = MockRegistry();
    final viewRefInstalled = MockViewRefInstalled();
    final injector = PointerInjector(registry, viewRefInstalled, device);

    final rect = Rect.fromLTWH(0, 0, 100, 100);
    final pointer = PointerDownEvent(position: Offset(10, 10));
    await injector.dispatchEvent(pointer: pointer, viewport: rect);

    final List<Event> events =
        verify(device.inject(captureAny!)).captured.single;
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
}

ViewRef _mockViewRef() {
  final viewRef = MockViewRef();
  final eventPair = MockEventPair();
  when(viewRef.reference).thenReturn(eventPair);
  when(eventPair.duplicate(any!)).thenReturn(eventPair);
  when(eventPair.isValid).thenReturn(true);

  return viewRef;
}

DeviceProxy _mockDevice() {
  final device = MockDevice();
  final ctrl = MockCtrl();
  when(device.ctrl).thenReturn(ctrl);
  when(ctrl.request()).thenReturn(null);
  return device;
}

class MockCtrl extends Mock implements AsyncProxyController<Device> {}

class MockDevice extends Mock implements DeviceProxy {}

class MockRegistry extends Mock implements Registry {}

class MockViewRefInstalled extends Mock implements ViewRefInstalled {}

class MockViewRef extends Mock implements ViewRef {}

class MockEventPair extends Mock implements EventPair {}
