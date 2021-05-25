// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'package:fidl_fuchsia_web/fidl_async.dart' as web;
import 'package:flutter/foundation.dart';

import 'package:fuchsia_logger/logger.dart';

import '../blocs/webpage_bloc.dart';

class SimpleBrowserNavigationEventListener extends web.NavigationEventListener {
  // Value Notifiers
  final _url = ValueNotifier<String>('');
  final _forwardState = ValueNotifier<bool>(false);
  final _backState = ValueNotifier<bool>(false);
  final _isLoadedState = ValueNotifier<bool>(true);
  final _pageTitle = ValueNotifier<String?>(null);
  final _pageType = ValueNotifier<PageType>(PageType.empty);

  ChangeNotifier get urlNotifier => _url;
  ChangeNotifier get forwardStateNotifier => _forwardState;
  ChangeNotifier get backStateNotifier => _backState;
  ChangeNotifier get isLoadedStateNotifier => _isLoadedState;
  ChangeNotifier get pageTitleNotifier => _pageTitle;
  ChangeNotifier get pageTypeNotifier => _pageType;

  String get url => _url.value;
  bool get forwardState => _forwardState.value;
  bool get backState => _backState.value;
  bool get isLoadedState => _isLoadedState.value;
  String? get pageTitle => _pageTitle.value;
  PageType get pageType => _pageType.value;

  SimpleBrowserNavigationEventListener();

  @override
  Future<Null> onNavigationStateChanged(web.NavigationState event) async {
    final url = event.url;
    if (url != null) {
      log.info('url loaded: $url');
      _url.value = url;
    }
    final canGoForward = event.canGoForward;
    if (canGoForward != null) {
      _forwardState.value = canGoForward;
    }
    final canGoBack = event.canGoBack;
    if (canGoBack != null) {
      _backState.value = canGoBack;
    }
    final isLoaded = event.isMainDocumentLoaded;
    if (isLoaded != null) {
      _isLoadedState.value = isLoaded;
    }
    final title = event.title;
    if (title != null) {
      _pageTitle.value = title;
    }
    final type = event.pageType;
    if (type != null) {
      _pageType.value = pageTypeForWebPageType(type);
    }
  }

  PageType pageTypeForWebPageType(web.PageType pageType) {
    switch (pageType) {
      case web.PageType.normal:
        return PageType.normal;
      case web.PageType.error:
        return PageType.error;
      default:
        return PageType.empty;
    }
  }
}
