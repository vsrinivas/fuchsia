// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: avoid_as, dead_code, null_check_always_fails

import 'dart:ui';

import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';
// ignore: implementation_imports
import 'package:fuchsia_scenic_flutter/src/pointer_injector.dart';
import 'package:mockito/mockito.dart';
import 'package:zircon/zircon.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('FuchsiaViewConnection', () async {
    bool? connectedCalled;
    bool? disconnectedCalled;
    final connection = TestFuchsiaViewConnection(
      _mockViewHolderToken(),
      onViewConnected: (_) => connectedCalled = true,
      onViewDisconnected: (_) => disconnectedCalled = true,
    );

    await connection.connect();
    verify(connection.fuchsiaViewsService.createView(
      42,
      hitTestable: true,
      focusable: true,
      viewOcclusionHint: Rect.zero,
    ));

    final methodCallback =
        verify(connection.fuchsiaViewsService.register(42, captureAny))
            .captured
            .single;
    expect(methodCallback, isNotNull);

    methodCallback(MethodCall('View.viewConnected'));
    methodCallback(MethodCall('View.viewDisconnected'));

    expect(connectedCalled, isTrue);
    expect(disconnectedCalled, isTrue);
  });

  test('FuchsiaViewConnection.usePointerInjection', () async {
    bool? connectedCalled;
    bool? disconnectedCalled;
    bool? stateChangedCalled;
    final connection = TestFuchsiaViewConnection(
      _mockViewHolderToken(),
      viewRef: _mockViewRef(),
      onViewConnected: (_) => connectedCalled = true,
      onViewDisconnected: (_) => disconnectedCalled = true,
      onViewStateChanged: (_, state) => stateChangedCalled = state,
      usePointerInjection: true,
    );

    // Invoke connect to capture [MethodCall] callback.
    await connection.connect();
    verify(connection.fuchsiaViewsService.createView(
      42,
      hitTestable: true,
      focusable: true,
      viewOcclusionHint: Rect.zero,
    ));

    when(connection.pointerInjector.registered).thenReturn(false);

    final methodCallback =
        verify(connection.fuchsiaViewsService.register(42, captureAny))
            .captured
            .single;
    expect(methodCallback, isNotNull);

    // Invoke all View.view* callbacks.
    methodCallback(MethodCall('View.viewConnected'));
    methodCallback(MethodCall('View.viewDisconnected'));
    methodCallback(MethodCall('View.viewStateChanged', {'state': true}));

    expect(connectedCalled, isTrue);
    expect(disconnectedCalled, isTrue);
    expect(stateChangedCalled, isTrue);
    verify(connection.pointerInjector.dispose());

    // Test pointer dispatch works.
    final downEvent = PointerDownEvent();
    when(connection.pointerInjector.registered).thenReturn(false);

    await connection.dispatchPointerEvent(downEvent);
    verify((connection.pointerInjector as MockPointerInjector).register(
      hostViewRef: anyNamed('hostViewRef'),
      viewRef: anyNamed('viewRef'),
      viewport: anyNamed('viewport'),
    ));

    when(connection.pointerInjector.registered).thenReturn(true);
    await connection.dispatchPointerEvent(downEvent);
    verify((connection.pointerInjector as MockPointerInjector).dispatchEvent(
        pointer: anyNamed('pointer'), viewport: anyNamed('viewport')));
  });

  test('Recreate pointer injection on error', () async {
    final connection = TestFuchsiaViewConnection(
      _mockViewHolderToken(),
      viewRef: _mockViewRef(),
      usePointerInjection: true,
    );

    // Error handler for pointer injection should dispose current instance of
    // PointerInjector.
    final injector = connection.pointerInjector;
    connection.onPointerInjectionError();
    verify(injector.dispose());

    // A new instance of PointerInjector should be created as part of register()
    // during dispatchPointerEvent call.
    connection._pointerInjector = MockPointerInjector();

    final downEvent = PointerDownEvent();
    when(connection.pointerInjector.registered).thenReturn(false);
    await connection.dispatchPointerEvent(downEvent);

    verify((connection.pointerInjector as MockPointerInjector).register(
      hostViewRef: anyNamed('hostViewRef'),
      viewRef: anyNamed('viewRef'),
      viewport: anyNamed('viewport'),
    ));

    // A new instance of PointerInjector should be used during register().
    final newInjector = connection.pointerInjector;
    expect(newInjector != injector, isTrue);
  });
}

ViewRef _mockViewRef() {
  final handle = MockHandle();
  when(handle.isValid).thenReturn(true);
  when(handle.duplicate(any)).thenReturn(handle);
  final eventPair = MockEventPair();
  when(eventPair.handle).thenReturn(handle);
  when(eventPair.isValid).thenReturn(true);
  final viewRef = MockViewRef();
  when(viewRef.reference).thenReturn(eventPair);
  return viewRef;
}

ViewHolderToken _mockViewHolderToken() {
  final handle = MockHandle();
  when(handle.handle).thenReturn(42);
  final eventPair = MockEventPair();
  when(eventPair.handle).thenReturn(handle);
  when(eventPair.isValid).thenReturn(true);
  final viewHolderToken = MockViewHolderToken();
  when(viewHolderToken.value).thenReturn(eventPair);

  return viewHolderToken;
}

class TestFuchsiaViewConnection extends FuchsiaViewConnection {
  final _fuchsiaViewsService = MockFuchsiaViewsService();
  // ignore: prefer_final_fields
  var _pointerInjector = MockPointerInjector();

  TestFuchsiaViewConnection(
    ViewHolderToken viewHolderToken, {
    ViewRef? viewRef,
    FuchsiaViewConnectionCallback? onViewConnected,
    FuchsiaViewConnectionCallback? onViewDisconnected,
    FuchsiaViewConnectionStateCallback? onViewStateChanged,
    bool usePointerInjection = false,
  }) : super(
          viewHolderToken,
          viewRef: viewRef,
          onViewConnected: onViewConnected,
          onViewDisconnected: onViewDisconnected,
          onViewStateChanged: onViewStateChanged,
          usePointerInjection: usePointerInjection,
        );

  @override
  FuchsiaViewsService get fuchsiaViewsService => _fuchsiaViewsService;

  @override
  PointerInjector get pointerInjector => _pointerInjector;

  @override
  ViewRef get hostViewRef => _mockViewRef();

  @override
  Rect? get viewport => Rect.fromLTWH(0, 0, 100, 100);
}

class MockFuchsiaViewsService extends Mock implements FuchsiaViewsService {}

class MockViewHolderToken extends Mock implements ViewHolderToken {
  @override
  int get hashCode => super.noSuchMethod(Invocation.method(#hashCode, []));

  @override
  bool operator ==(dynamic other) =>
      super.noSuchMethod(Invocation.method(#==, [other]));
}

class MockViewRef extends Mock implements ViewRef {
  @override
  int get hashCode => super.noSuchMethod(Invocation.method(#hashCode, []));

  @override
  bool operator ==(dynamic other) =>
      super.noSuchMethod(Invocation.method(#==, [other]));
}

class MockEventPair extends Mock implements EventPair {}

class MockHandle extends Mock implements Handle {
  @override
  int get hashCode => super.noSuchMethod(Invocation.method(#hashCode, []));

  @override
  bool operator ==(dynamic other) =>
      super.noSuchMethod(Invocation.method(#==, [other]));

  @override
  Handle duplicate(int? rights) =>
      super.noSuchMethod(Invocation.method(#==, [rights]));
}

class MockPointerInjector extends Mock implements PointerInjector {
  @override
  Future<void> register({
    ViewRef? hostViewRef,
    ViewRef? viewRef,
    Rect? viewport,
  }) async =>
      super.noSuchMethod(Invocation.method(#register, [], {
        #hostViewRef: hostViewRef,
        #viewRef: viewRef,
        #viewport: viewport,
      }));

  @override
  Future<void> dispatchEvent({
    PointerEvent? pointer,
    Rect? viewport,
  }) async =>
      super.noSuchMethod(Invocation.method(#dispatchEvent, [], {
        #pointer: pointer,
        #viewport: viewport,
      }));
}
