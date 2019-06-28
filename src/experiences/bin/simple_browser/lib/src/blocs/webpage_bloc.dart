// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'package:flutter/foundation.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_scenic_flutter/child_view.dart'
    show ChildViewConnection;
import 'package:fidl_fuchsia_web/fidl_async.dart' as web;
import 'package:webview/webview.dart';
import '../models/webpage_action.dart';

// Business logic for the webpage.
// Sinks:
//   WebPageAction: a browsing action - url request, prev/next page, etc.
// Value Notifiers:
//   Url: the current url.
//   ForwardState: bool indicating whether forward action is available.
//   BackState: bool indicating whether back action is available.
//   isLoadedState: bool indicating whether main document has fully loaded.
class WebPageBloc extends web.NavigationEventListener {
  final ChromiumWebView _webView;

  ChildViewConnection get childViewConnection => _webView.childViewConnection;

  // Value Notifiers
  final ValueNotifier<String> url = ValueNotifier<String>('');
  final ValueNotifier<bool> forwardState = ValueNotifier<bool>(false);
  final ValueNotifier<bool> backState = ValueNotifier<bool>(false);
  final ValueNotifier<bool> isLoadedState = ValueNotifier<bool>(true);
  final ValueNotifier<String> pageTitle = ValueNotifier<String>(null);

  // Sinks
  final _webPageActionController = StreamController<WebPageAction>();
  Sink<WebPageAction> get request => _webPageActionController.sink;

  WebPageBloc({
    String homePage,
    web.ContextProxy context,
  }) : _webView = ChromiumWebView.withContext(context: context) {
    _webView.setNavigationEventListener(this);

    if (homePage != null) {
      _handleAction(NavigateToAction(url: homePage));
    }
    _webPageActionController.stream.listen(_handleAction);
  }

  void dispose() {
    _webView.dispose();
    _webPageActionController.close();
  }

  @override
  Future<Null> onNavigationStateChanged(web.NavigationState event) async {
    if (event.url != null) {
      log.info('url loaded: ${event.url}');
      url.value = event.url;
    }
    if (event.canGoForward != null) {
      forwardState.value = event.canGoForward;
    }
    if (event.canGoBack != null) {
      backState.value = event.canGoBack;
    }
    if (event.isMainDocumentLoaded != null) {
      isLoadedState.value = event.isMainDocumentLoaded;
    }
    if (event.title != null) {
      pageTitle.value = event.title;
    }
  }

  Future<void> _handleAction(WebPageAction action) async {
    switch (action.op) {
      case WebPageActionType.navigateTo:
        final NavigateToAction navigate = action;
        await _webView.controller.loadUrl(
          _sanitizeUrl(navigate.url),
          web.LoadUrlParams(type: web.LoadUrlReason.typed),
        );
        break;
      case WebPageActionType.goBack:
        await _webView.controller.goBack();
        break;
      case WebPageActionType.goForward:
        await _webView.controller.goForward();
        break;
    }
  }

  String _sanitizeUrl(String url) {
    if (url.startsWith('http')) {
      return url;
    } else if (url.endsWith('.com')) {
      return 'https://$url';
    } else {
      return 'https://www.google.com/search?q=${Uri.encodeQueryComponent(url)}';
    }
  }
}
