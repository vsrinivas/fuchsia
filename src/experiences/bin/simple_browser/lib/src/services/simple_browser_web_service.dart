// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'package:fidl/fidl.dart' show InterfaceHandle;
import 'package:fidl_fuchsia_ui_views/fidl_async.dart' as views;
import 'package:fidl_fuchsia_web/fidl_async.dart' as web;
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart'
    show FuchsiaViewConnection;
import 'package:zircon/zircon.dart';

import '../blocs/webpage_bloc.dart';
import 'simple_browser_navigation_event_listener.dart';

class SimpleBrowserWebService {
  final web.FrameProxy _frame;
  final _navigationController = web.NavigationControllerProxy();
  final _navigationEventObserverBinding = web.NavigationEventListenerBinding();
  final _popupFrameCreationObserverBinding =
      web.PopupFrameCreationListenerBinding();
  final _simpleBrowserNavigationEventListener =
      SimpleBrowserNavigationEventListener();

  /// Used to present webpage in Flutter FuchsiaView
  FuchsiaViewConnection get fuchsiaViewConnection => _fuchsiaViewConnection;
  late FuchsiaViewConnection _fuchsiaViewConnection;

  late views.ViewHolderToken _viewHolderToken;

  SimpleBrowserNavigationEventListener get navigationEventListener =>
      _simpleBrowserNavigationEventListener;

  factory SimpleBrowserWebService({
    required web.ContextProxy context,
    required void Function(WebPageBloc webPageBloc) popupHandler,
  }) {
    final frame = web.FrameProxy();
    context.createFrame(frame.ctrl.request());
    return SimpleBrowserWebService.withFrame(
      frame: frame,
      popupHandler: popupHandler,
    );
  }

  SimpleBrowserWebService.withFrame({
    required web.FrameProxy frame,
    required void Function(WebPageBloc webPageBloc) popupHandler,
  }) : _frame = frame {
    _frame

      /// Sets up listeners and attaches navigation controller.
      ..setNavigationEventListener(_navigationEventObserverBinding
          .wrap(_simpleBrowserNavigationEventListener))
      ..getNavigationController(_navigationController.ctrl.request())
      ..setPopupFrameCreationListener(
        _popupFrameCreationObserverBinding.wrap(
          _PopupListener(popupHandler),
        ),
      );

    /// Creates a token pair for the newly-created View.
    final tokenPair = EventPairPair();
    assert(tokenPair.status == ZX.OK);
    _viewHolderToken = views.ViewHolderToken(value: tokenPair.first!);

    final viewToken = views.ViewToken(value: tokenPair.second!);

    _frame.createView(viewToken);
    _fuchsiaViewConnection = FuchsiaViewConnection(_viewHolderToken);
  }

  Future<void> enableConsoleLog() =>
      _frame.setJavaScriptLogLevel(web.ConsoleLogLevel.debug);

  void dispose() {
    _navigationController.ctrl.close();
    _frame.ctrl.close();
  }

  // TODO(fxr/77454): Make [url] required once the 'any' keyword in mockito is
  // available in unit tests for null-safe packages.
  Future<void> loadUrl(String url) => _navigationController.loadUrl(
        url,
        web.LoadUrlParams(type: web.LoadUrlReason.typed),
      );
  Future<void> goBack() => _navigationController.goBack();
  Future<void> goForward() => _navigationController.goForward();
  Future<void> refresh() =>
      _navigationController.reload(web.ReloadType.partialCache);
}

class _PopupListener extends web.PopupFrameCreationListener {
  final void Function(WebPageBloc webPageBloc) _handler;

  _PopupListener(this._handler);

  @override
  Future<void> onPopupFrameCreated(
    InterfaceHandle<web.Frame> frame,
    web.PopupFrameCreationInfo info,
  ) async {
    final webService = SimpleBrowserWebService.withFrame(
      frame: web.FrameProxy()..ctrl.bind(frame),
      popupHandler: _handler,
    );
    _handler(
      WebPageBloc(webService: webService),
    );
  }
}
