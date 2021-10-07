// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: avoid_as, import_of_legacy_library_into_null_safe

import 'dart:async';
import 'dart:convert' show utf8;
import 'dart:typed_data';

import 'package:fidl_fuchsia_net_http/fidl_async.dart' as fidl_net;
import 'package:fidl_fuchsia_web/fidl_async.dart' as fidl_web;
import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';
import 'package:fuchsia_webview_flutter/src/fuchsia_web_services.dart';
import 'package:fuchsia_webview_flutter/src/fuchsia_webview_platform_controller.dart';
import 'package:fuchsia_webview_flutter/webview.dart';
import 'package:mockito/mockito.dart';
import 'package:webview_flutter/webview_flutter.dart';

// ignore_for_file: implementation_imports

class MockFuchsiaWebServices extends Mock implements FuchsiaWebServices {}

class MockWebViewPlatformCallbacksHandler extends Mock
    implements WebViewPlatformCallbacksHandler {}

class MockFuchsiaWebViewPlatformController extends Mock
    implements FuchsiaWebViewPlatformController {}

class MockNavigationControllerProxy extends Mock
    implements fidl_web.NavigationControllerProxy {}

class MockFuchsiaViewConnection extends Mock implements FuchsiaViewConnection {}

void main() {
  FuchsiaWebServices? mockWebServices;
  fidl_web.NavigationControllerProxy? mockNavigationController;
  MockFuchsiaViewConnection mockFuchsiaViewConnection;

  setUp(() {
    mockWebServices = MockFuchsiaWebServices();
    mockNavigationController = MockNavigationControllerProxy();
    mockFuchsiaViewConnection = MockFuchsiaViewConnection();
    when(mockWebServices!.navigationController)
        .thenReturn(mockNavigationController!);
    when(mockWebServices!.viewConnection).thenReturn(mockFuchsiaViewConnection);

    final completer = Completer();
    when(mockFuchsiaViewConnection.viewId).thenReturn(42);
    when(mockFuchsiaViewConnection.whenConnected)
        .thenAnswer((_) => Future<bool>.value(false));
    when(mockFuchsiaViewConnection.connect())
        .thenAnswer((_) => completer.future);

    WebView.platform = FuchsiaWebView(fuchsiaWebServices: mockWebServices);
  });

  tearDown(() {
    WebView.platform = null;
  });

  testWidgets('Create WebView', (WidgetTester tester) async {
    await tester.pumpWidget(const WebView());
  });

  group('navigation: ', () {
    late WebViewController webViewController;
    late WebView webView;

    setUp(() {
      webView = WebView(
        onWebViewCreated: (WebViewController webViewCtl) {
          webViewController = webViewCtl;
        },
      );
    });

    testWidgets('loadUrl', (WidgetTester tester) async {
      await tester.pumpWidget(webView);

      final headers = <String, String>{'header': 'value'};
      String url = 'https://google.com';
      await webViewController.loadUrl(url, headers: headers);

      verify(mockNavigationController!.loadUrl(
          url,
          fidl_web.LoadUrlParams(
            type: fidl_web.LoadUrlReason.typed,
            headers: [
              fidl_net.Header(
                  name: utf8.encode('header') as Uint8List,
                  value: utf8.encode('value') as Uint8List)
            ],
          )));
    });

    testWidgets('goBack', (WidgetTester tester) async {
      await tester.pumpWidget(webView);

      await webViewController.goBack();
      verify(mockNavigationController!.goBack());
    });

    testWidgets('goForward', (WidgetTester tester) async {
      await tester.pumpWidget(webView);

      await webViewController.goForward();
      verify(mockNavigationController!.goForward());
    });

    testWidgets('reload', (WidgetTester tester) async {
      await tester.pumpWidget(webView);

      await webViewController.reload();
      verify(
          mockNavigationController!.reload(fidl_web.ReloadType.partialCache));
    });

    testWidgets('disposed when removed from widget tree',
        (WidgetTester tester) async {
      final includeWebview = ValueNotifier<bool>(true);
      await tester.pumpWidget(ValueListenableBuilder(
          valueListenable: includeWebview,
          builder: (_, dynamic includeWebviewValue, __) {
            return includeWebviewValue ? webView : Container();
          }));
      await tester.pumpAndSettle();

      verifyNever(mockWebServices!.dispose());

      includeWebview.value = false;
      await tester.pumpAndSettle();

      verify(mockWebServices!.dispose());
    });
  });

  group('JS injection', () {
    late WebViewController webViewController;
    late WebView webView;

    setUp(() {
      webView = WebView(
        onWebViewCreated: (WebViewController webViewCtl) {
          webViewController = webViewCtl;
        },
        javascriptMode: JavascriptMode.unrestricted,
      );
    });

    testWidgets('evaluateJavascript', (WidgetTester tester) async {
      await tester.pumpWidget(webView);
      const script = 'console.log("hello");';
      await webViewController.evaluateJavascript(script);
      verify(mockWebServices!.evaluateJavascript(['*'], script));
    });
  });

  group('FuchsiaWebServices config', () {
    test('default config', () {
      expect(
          FuchsiaWebServices.webFeaturesFromSettings(),
          FuchsiaWebServices.baseWebFeatures |
              fidl_web.ContextFeatureFlags.vulkan);
    });

    test('software false', () {
      expect(
          FuchsiaWebServices.webFeaturesFromSettings(
              useSoftwareRendering: false),
          FuchsiaWebServices.baseWebFeatures |
              fidl_web.ContextFeatureFlags.vulkan);
    });

    test('software true', () {
      expect(
          FuchsiaWebServices.webFeaturesFromSettings(
              useSoftwareRendering: true),
          FuchsiaWebServices.baseWebFeatures);
    });
  });
}
