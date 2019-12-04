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
import '../utils/sanitize_url.dart';

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

/// Business logic for the webpage.
/// Sinks:
///   WebPageAction: a browsing action - url request, prev/next page, etc.
/// Value Notifiers:
///   url: the current url.
///   forwardState: bool indicating whether forward action is available.
///   backState: bool indicating whether back action is available.
///   isLoadedState: bool indicating whether main document has fully loaded.
///   pageTitle: the current page title.
///   pageType: the current type of the page; either normal, error, or empty.
class WebPageBloc extends web.NavigationEventListener {
  /// Used to present webpage in Flutter ChildView
  ChildViewConnection get childViewConnection => _childViewConnection;
  ChildViewConnection _childViewConnection;

  final web.FrameProxy frame;
  final web.NavigationControllerProxy _navigationController;
  final web.NavigationEventListenerBinding _navigationEventObserverBinding;
  final web.PopupFrameCreationListenerBinding
      _popupFrameCreationObserverBinding;

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

  /// Creates a new [WebPageBloc] with a new page from [ContextProxy].
  ///
  /// A basic constructor for creating a brand-new tab.
  /// Can also be used for testing purposes and in this case,
  /// context parameter does not need to be set.
  WebPageBloc({
    web.ContextProxy context,
    String homePage,
    web.NavigationControllerProxy controller,
    web.NavigationEventListenerBinding listener,
    web.PopupFrameCreationListenerBinding popupListener,
    void Function(WebPageBloc popup) popupHandler,
  })  

  // TODO(fxb/42424): Refactor WebPageBloc to be more test-friendly.
  : frame = (context != null) ? web.FrameProxy() : null,
        _navigationController = controller ?? web.NavigationControllerProxy(),
        _navigationEventObserverBinding =
            listener ?? web.NavigationEventListenerBinding(),
        _popupFrameCreationObserverBinding =
            popupListener ?? web.PopupFrameCreationListenerBinding() {
    if (context != null) {
      context.createFrame(frame.ctrl.request());
      _setUp(popupHandler);
    }
    if (homePage != null) {
      _handleAction(NavigateToAction(url: homePage));
    }

    /// Begins handling action requests
    _webPageActionController.stream.listen(_handleAction);
  }

  /// Creates a new [WebPageBloc] with [FrameProxy].
  ///
  /// This constructor is normally used for pop-ups.
  WebPageBloc.withFrame({
    @required this.frame,
    @required void Function(WebPageBloc popup) popupHandler,
  })  : assert(frame != null),
        assert(popupHandler != null),
        _navigationController = web.NavigationControllerProxy(),
        _navigationEventObserverBinding = web.NavigationEventListenerBinding(),
        _popupFrameCreationObserverBinding =
            web.PopupFrameCreationListenerBinding() {
    /// Begins handling action requests
    _setUp(popupHandler);
    _webPageActionController.stream.listen(_handleAction);
  }

  void _setUp(
    void Function(WebPageBloc popup) popupHandler,
  ) {
    frame

      /// Sets up listeners and attaches navigation controller.
      ..setNavigationEventListener(_navigationEventObserverBinding.wrap(this))
      ..getNavigationController(_navigationController.ctrl.request());
    if (_popupFrameCreationObserverBinding != null && popupHandler != null) {
      frame.setPopupFrameCreationListener(
        _popupFrameCreationObserverBinding.wrap(
          _PopupListener(popupHandler),
        ),
      );
    }

    /// Creates a token pair for the newly-created View.
    final tokenPair = EventPairPair();
    assert(tokenPair.status == ZX.OK);
    final viewHolderToken = views.ViewHolderToken(value: tokenPair.first);
    final viewToken = views.ViewToken(value: tokenPair.second);

    frame.createView(viewToken);
    _childViewConnection = ChildViewConnection(viewHolderToken);
  }

  void dispose() {
    _navigationController.ctrl.close();
    frame.ctrl.close();
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
          sanitizeUrl(navigate.url),
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
