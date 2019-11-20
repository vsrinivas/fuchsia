// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'package:fidl/fidl.dart' show InterfaceHandle;
import 'package:flutter/foundation.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_scenic_flutter/child_view.dart'
    show ChildViewConnection;
import 'package:fidl_fuchsia_ui_views/fidl_async.dart' as views;
import 'package:fidl_fuchsia_web/fidl_async.dart' as web;
import 'package:zircon/zircon.dart';

import '../models/webpage_action.dart';

enum PageType { empty, normal, error }
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

// Business logic for the webpage.
// Sinks:
//   WebPageAction: a browsing action - url request, prev/next page, etc.
// Value Notifiers:
//   url: the current url.
//   forwardState: bool indicating whether forward action is available.
//   backState: bool indicating whether back action is available.
//   isLoadedState: bool indicating whether main document has fully loaded.
//   pageTitle: the current page title.
//   pageType: the current type of the page; either normal, error, or empty.
class WebPageBloc extends web.NavigationEventListener {
  /// Used to present webpage in Flutter ChildView
  ChildViewConnection get childViewConnection => _childViewConnection;
  ChildViewConnection _childViewConnection;

  final web.FrameProxy _frame;
  final _navigationController = web.NavigationControllerProxy();
  final _navigationEventObserverBinding = web.NavigationEventListenerBinding();
  final _popupFrameCreationObserverBinding =
      web.PopupFrameCreationListenerBinding();

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

  // Sinks
  final _webPageActionController = StreamController<WebPageAction>();
  Sink<WebPageAction> get request => _webPageActionController.sink;

  // Constructors

  /// Create a new [WebPageBloc] with a new page from [context]
  WebPageBloc({
    String homePage,
    web.ContextProxy context,
    void Function(WebPageBloc popup) popupHandler,
  }) : _frame = web.FrameProxy() {
    context.createFrame(_frame.ctrl.request());
    _setup(popupHandler: popupHandler);

    if (homePage != null) {
      _handleAction(NavigateToAction(url: homePage));
    }
  }

  /// Create a new [WebPageBloc] with [frame]
  WebPageBloc.withFrame({
    @required web.FrameProxy frame,
    void Function(WebPageBloc popup) popupHandler,
  }) : _frame = frame {
    _setup(popupHandler: popupHandler);
  }

  /// Common setup for all constructors
  void _setup({
    void Function(WebPageBloc popup) popupHandler,
  }) {
    _frame

      // setup listeners
      ..setNavigationEventListener(_navigationEventObserverBinding.wrap(this))
      ..setPopupFrameCreationListener(
          _popupFrameCreationObserverBinding.wrap(_PopupListener(popupHandler)))

      // attach navigation controller
      ..getNavigationController(_navigationController.ctrl.request());

    // Create a token pair for the newly-created View.
    final tokenPair = EventPairPair();
    assert(tokenPair.status == ZX.OK);
    final viewHolderToken = views.ViewHolderToken(value: tokenPair.first);
    final viewToken = views.ViewToken(value: tokenPair.second);

    _frame.createView(viewToken);
    _childViewConnection = ChildViewConnection(viewHolderToken);

    // begin handling action requests
    _webPageActionController.stream.listen(_handleAction);
  }

  void dispose() {
    _navigationController.ctrl.close();
    _frame.ctrl.close();
    _webPageActionController.close();
  }

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

  Future<void> _handleAction(WebPageAction action) async {
    switch (action.op) {
      case WebPageActionType.navigateTo:
        final NavigateToAction navigate = action;
        await _navigationController.loadUrl(
          _sanitizeUrl(navigate.url),
          web.LoadUrlParams(type: web.LoadUrlReason.typed),
        );
        break;
      case WebPageActionType.goBack:
        await _navigationController.goBack();
        break;
      case WebPageActionType.goForward:
        await _navigationController.goForward();
        break;
      case WebPageActionType.refresh:
        await _navigationController.reload(web.ReloadType.partialCache);
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

class _PopupListener extends web.PopupFrameCreationListener {
  final void Function(WebPageBloc popup) _handler;

  _PopupListener(this._handler);

  @override
  Future<void> onPopupFrameCreated(
    InterfaceHandle<web.Frame> frame,
    web.PopupFrameCreationInfo info,
  ) async {
    _handler(
      WebPageBloc.withFrame(
        frame: web.FrameProxy()..ctrl.bind(frame),
        popupHandler: _handler,
      ),
    );
  }
}
