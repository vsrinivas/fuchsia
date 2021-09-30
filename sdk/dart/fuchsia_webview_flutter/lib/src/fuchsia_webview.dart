// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: import_of_legacy_library_into_null_safe

import 'package:flutter/foundation.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/widgets.dart';
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';
import 'package:webview_flutter/platform_interface.dart';

import 'fuchsia_web_services.dart';
import 'fuchsia_webview_platform_controller.dart';

/// Builds an Fuchsia webview.
class FuchsiaWebView implements WebViewPlatform {
  /// The fuchsia implementation of [WebViewPlatformController]
  FuchsiaWebServices? fuchsiaWebServices;

  /// This constructor should only be used to inject a platform controller for
  /// testing.
  ///
  /// TODO(nkorsote): hide this implementation detail
  @visibleForTesting
  FuchsiaWebView({this.fuchsiaWebServices});

  /// This constructor creates a FuchsiaWebView, parameterizing it with the desired features.
  FuchsiaWebView.create({bool useSoftwareRendering = false})
      : fuchsiaWebServices =
            FuchsiaWebServices(useSoftwareRendering: useSoftwareRendering);

  @override
  Widget build({
    required BuildContext context,
    required CreationParams creationParams,
    required WebViewPlatformCallbacksHandler webViewPlatformCallbacksHandler,
    required JavascriptChannelRegistry javascriptChannelRegistry,
    WebViewPlatformCreatedCallback? onWebViewPlatformCreated,
    Set<Factory<OneSequenceGestureRecognizer>>? gestureRecognizers,
  }) {
    return _EmbeddedWebview(
      creationParams: creationParams,
      webViewPlatformCallbacksHandler: webViewPlatformCallbacksHandler,
      javascriptChannelRegistry: javascriptChannelRegistry,
      onWebViewPlatformCreated: onWebViewPlatformCreated,
      fuchsiaWebServices: fuchsiaWebServices,
    );
  }

  @override
  Future<bool> clearCookies() =>
      FuchsiaWebViewPlatformController.clearCookies();
}

class _EmbeddedWebview extends StatefulWidget {
  final CreationParams creationParams;
  final WebViewPlatformCallbacksHandler webViewPlatformCallbacksHandler;
  final JavascriptChannelRegistry javascriptChannelRegistry;
  final WebViewPlatformCreatedCallback? onWebViewPlatformCreated;
  final FuchsiaWebServices? fuchsiaWebServices;

  const _EmbeddedWebview({
    required this.creationParams,
    required this.webViewPlatformCallbacksHandler,
    required this.javascriptChannelRegistry,
    this.onWebViewPlatformCreated,
    this.fuchsiaWebServices,
  });
  @override
  _EmbeddedWebviewState createState() => _EmbeddedWebviewState();
}

class _EmbeddedWebviewState extends State<_EmbeddedWebview> {
  late FuchsiaWebViewPlatformController _controller;

  @override
  Widget build(BuildContext context) =>
      FuchsiaView(controller: _controller.fuchsiaWebServices.viewConnection!);

  @override
  void initState() {
    super.initState();
    _controller = FuchsiaWebViewPlatformController(
      widget.creationParams,
      widget.webViewPlatformCallbacksHandler,
      widget.javascriptChannelRegistry,
      widget.fuchsiaWebServices,
    );
    widget.onWebViewPlatformCreated?.call(_controller);
  }

  @override
  void dispose() {
    super.dispose();
    _controller.dispose();
  }
}
