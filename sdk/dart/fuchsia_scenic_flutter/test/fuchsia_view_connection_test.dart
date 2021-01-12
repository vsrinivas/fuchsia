// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';
import 'package:fuchsia_services/services.dart';
import 'package:mockito/mockito.dart';
import 'package:zircon/zircon.dart';

// ignore: implementation_imports
import 'package:fuchsia_scenic_flutter/src/pointer_injector.dart';

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
    verify(connection.platformViewChannel.invokeMethod('View.create', {
      'viewId': 42,
      'hitTestable': true,
      'focusable': true,
    }));

    final methodCallback =
        verify(connection.platformViewChannel.setMethodCallHandler(captureAny))
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
    final connection = TestFuchsiaViewConnection(
      _mockViewHolderToken(),
      viewRef: _mockViewRef(),
      onViewConnected: (_) => connectedCalled = true,
      onViewDisconnected: (_) => disconnectedCalled = true,
      usePointerInjection: true,
    );

    final handle = MockHandle();
    when(handle.duplicate(any!)).thenReturn(handle);
    when(connection.startupContext.viewRef).thenReturn(handle);

    await connection.connect();
    verify(connection.platformViewChannel.invokeMethod('View.create', {
      'viewId': 42,
      'hitTestable': true,
      'focusable': true,
    }));

    final methodCallback =
        verify(connection.platformViewChannel.setMethodCallHandler(captureAny))
            .captured
            .single;
    expect(methodCallback, isNotNull);

    methodCallback(MethodCall('View.viewConnected'));
    methodCallback(MethodCall('View.viewDisconnected'));

    expect(connectedCalled, isTrue);
    expect(disconnectedCalled, isTrue);

    verify(connection.pointerInjector.register(
      hostViewRef: anyNamed('hostViewRef')!,
      viewRef: anyNamed('viewRef')!,
      viewport: anyNamed('viewport')!,
    ));
    verify(connection.pointerInjector.dispose());

    // Test pointer dispatch works.
    final addedEvent = PointerAddedEvent();
    await connection.dispatchPointerEvent(addedEvent);
    verify(connection.pointerInjector.dispatchEvent(
        pointer: anyNamed('pointer')!, viewport: anyNamed('viewport')));
  });
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

ViewRef _mockViewRef() {
  final handle = MockHandle();
  when(handle.isValid).thenReturn(true);
  when(handle.duplicate(any!)).thenReturn(handle);
  final eventPair = MockEventPair();
  when(eventPair.handle).thenReturn(handle);
  when(eventPair.isValid).thenReturn(true);
  final viewRef = MockViewRef();
  when(viewRef.reference).thenReturn(eventPair);
  return viewRef;
}

class TestFuchsiaViewConnection extends FuchsiaViewConnection {
  final _platformMethodChannel = MockMethodChannel();
  final _startupContext = MockStartupContext();
  final _pointerInjector = MockPointerInjector();

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
  MethodChannel get platformViewChannel => _platformMethodChannel;

  @override
  StartupContext get startupContext => _startupContext;

  @override
  PointerInjector get pointerInjector => _pointerInjector;
}

class MockMethodChannel extends Mock implements MethodChannel {}

class MockViewHolderToken extends Mock implements ViewHolderToken {}

class MockViewRef extends Mock implements ViewRef {}

class MockEventPair extends Mock implements EventPair {}

class MockHandle extends Mock implements Handle {}

class MockStartupContext extends Mock implements StartupContext {}

class MockPointerInjector extends Mock implements PointerInjector {}
