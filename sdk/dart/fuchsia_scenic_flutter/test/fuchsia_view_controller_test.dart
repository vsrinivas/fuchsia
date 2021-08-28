// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';
import 'dart:ui';

import 'package:flutter/gestures.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';

void main() {
  const testViewId = 42;
  const testViewRef = 24;

  Future<void> sendViewConnectedEvent(int viewId) {
    return FuchsiaViewsService.instance.platformViewChannel.binaryMessenger
        .handlePlatformMessage(
            FuchsiaViewsService.instance.platformViewChannel.name,
            FuchsiaViewsService.instance.platformViewChannel.codec
                .encodeMethodCall(MethodCall(
                    'View.viewConnected', <String, dynamic>{'viewId': viewId})),
            null);
  }

  Future<void> sendViewDisconnectedEvent(int viewId) {
    return FuchsiaViewsService.instance.platformViewChannel.binaryMessenger
        .handlePlatformMessage(
            FuchsiaViewsService.instance.platformViewChannel.name,
            FuchsiaViewsService.instance.platformViewChannel.codec
                .encodeMethodCall(MethodCall('View.viewDisconnected',
                    <String, dynamic>{'viewId': viewId})),
            null);
  }

  Future<void> sendViewStateChangedEvent(int viewId,
      {bool isRendering = true}) {
    return FuchsiaViewsService.instance.platformViewChannel.binaryMessenger
        .handlePlatformMessage(
            FuchsiaViewsService.instance.platformViewChannel.name,
            FuchsiaViewsService.instance.platformViewChannel.codec
                .encodeMethodCall(MethodCall(
                    'View.viewStateChanged', <String, dynamic>{
              'viewId': viewId,
              'is_rendering': isRendering,
              'state': isRendering
            })),
            null);
  }

  void expectPlatformViewChannelCall(
      WidgetTester tester, String methodName, Map<String, dynamic> methodArgs,
      {dynamic Function()? handler}) {
    tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(
        FuchsiaViewsService.instance.platformViewChannel, (call) async {
      expect(call.method, methodName);
      expect(call.arguments, methodArgs);
      if (handler != null) {
        return handler.call();
      }
    });
  }

  void expectNoPlatformViewChannelCalls(WidgetTester tester) {
    tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(
        FuchsiaViewsService.instance.platformViewChannel, (call) async {
      fail('Expected no calls on platformViewChannel but got $call');
    });
  }

  testWidgets('FuchsiaViewController lifecycle', (WidgetTester tester) async {
    bool connectedCalled = false;
    bool disconnectedCalled = false;
    bool stateChangedCalled = false;
    final controller = FuchsiaViewController(
      viewId: testViewId,
      onViewConnected: (_) => connectedCalled = true,
      onViewDisconnected: (_) => disconnectedCalled = true,
      onViewStateChanged: (_, __) => stateChangedCalled = true,
    );

    expectPlatformViewChannelCall(tester, 'View.create', <String, dynamic>{
      'viewId': testViewId,
      'hitTestable': true,
      'focusable': true,
      'viewOcclusionHintLTRB': <double>[0, 0, 0, 0],
    });
    await controller.connect();
    expect(controller.connected, false);
    expect(connectedCalled, false);
    expect(disconnectedCalled, false);
    expect(stateChangedCalled, false);

    // connect() does nothing after the first call.
    expectNoPlatformViewChannelCalls(tester);
    await controller.connect();

    // signal that the platform view has connected successfully.
    await sendViewConnectedEvent(testViewId);
    expect(controller.connected, true);
    expect(connectedCalled, true);

    // connect() does nothing after the first call, even if the view has connected already.
    expectNoPlatformViewChannelCalls(tester);
    await controller.connect();

    // update() the view while it is connected.
    expectPlatformViewChannelCall(tester, 'View.update', <String, dynamic>{
      'viewId': testViewId,
      'hitTestable': false,
      'focusable': false,
      'viewOcclusionHintLTRB': <double>[10, 10, 20, 30],
    });
    await controller.update(
        focusable: false,
        hitTestable: false,
        viewOcclusionHint: Rect.fromLTRB(10, 10, 20, 30));

    // signal that the platform view is rendering
    await sendViewStateChangedEvent(testViewId, isRendering: true);
    expect(stateChangedCalled, true);

    // signal that the platform view has disconnected.
    await sendViewDisconnectedEvent(testViewId);
    expect(controller.connected, false);
    expect(disconnectedCalled, true);

    // connect() does nothing after the first call, even if the view has disconnected already.
    expectNoPlatformViewChannelCalls(tester);
    await controller.connect();
    expect(controller.connected, false);

    // Manually dispose() the platform view.
    expectPlatformViewChannelCall(tester, 'View.dispose', <String, dynamic>{
      'viewId': testViewId,
    });
    await controller.dispose();

    // Disposing twice does nothing after the first call.
    expectNoPlatformViewChannelCalls(tester);
    await controller.dispose();
  });

  testWidgets('FuchsiaViewController dispatchPointerEvent',
      (WidgetTester tester) async {
    PointerEvent? event;
    final controller = FuchsiaViewController(
      viewId: testViewId,
      onPointerEvent: (_, e) async => event = e,
    );

    final addedEvent = PointerAddedEvent();
    await controller.dispatchPointerEvent(addedEvent);
    expect(event, equals(addedEvent));
  });

  testWidgets('FuchsiaViewController requestFocus',
      (WidgetTester tester) async {
    final controller = FuchsiaViewController(viewId: testViewId);

    bool requestFocusCalled = false;
    int requestFocusReturnVal = 0;
    expectPlatformViewChannelCall(
        tester, 'View.focus.request', <String, dynamic>{
      'viewRef': testViewRef,
    }, handler: () {
      requestFocusCalled = true;
      return requestFocusReturnVal;
    });

    // Test requestFocus successfully sets focus.
    requestFocusCalled = false;
    requestFocusReturnVal = 0;
    await controller.requestFocus(testViewRef);
    expect(requestFocusCalled, true);

    // Test requestFocus fails to set focus.
    bool exceptionThrown = false;
    requestFocusCalled = false;
    requestFocusReturnVal = 1;
    try {
      await controller.requestFocus(testViewRef);
    } on OSError {
      exceptionThrown = true;
    }
    expect(requestFocusCalled, true);
    expect(exceptionThrown, true);
  });
}
