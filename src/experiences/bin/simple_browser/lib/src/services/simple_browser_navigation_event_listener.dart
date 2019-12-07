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
  final _pageTitle = ValueNotifier<String>(null);
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
  String get pageTitle => _pageTitle.value;
  PageType get pageType => _pageType.value ?? PageType.empty;

  SimpleBrowserNavigationEventListener();

  @override
  Future<Null> onNavigationStateChanged(web.NavigationState event) async {
    if (event.url != null) {
      log.info('url loaded: ${event.url}');
      _url.value = event.url;
    }
    if (event.canGoForward != null) {
      _forwardState.value = event.canGoForward;
    }
    if (event.canGoBack != null) {
      _backState.value = event.canGoBack;
    }
    if (event.isMainDocumentLoaded != null) {
      _isLoadedState.value = event.isMainDocumentLoaded;
    }
    if (event.title != null) {
      _pageTitle.value = event.title;
    }
    if (event.pageType != null) {
      _pageType.value = pageTypeForWebPageType(event.pageType);
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
