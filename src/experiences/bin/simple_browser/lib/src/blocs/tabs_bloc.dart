// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:collection';
import 'package:fidl_fuchsia_web/fidl_async.dart' as web show ContextProxy;
import 'package:flutter/foundation.dart';
import 'package:webview/webview.dart';
import '../models/tabs_action.dart';
import 'webpage_bloc.dart';

// Business logic for browser tabs.
// Sinks:
//   TabsAction: a tabs action - open new tab, focus tab, etc.
// Value Notifiers:
//   Tabs: the list of open tabs.
//   CurrentTab: the currently focused tab.
class TabsBloc {
  final web.ContextProxy _context;

  final _tabsList = <WebPageBloc>[];

  // Value Notifiers
  final ValueNotifier<UnmodifiableListView<WebPageBloc>> tabs =
      ValueNotifier<UnmodifiableListView<WebPageBloc>>(
          UnmodifiableListView(<WebPageBloc>[]));
  final ValueNotifier<WebPageBloc> currentTab =
      ValueNotifier<WebPageBloc>(null);

  // Sinks
  final _tabsActionController = StreamController<TabsAction>();
  Sink<TabsAction> get request => _tabsActionController.sink;

  TabsBloc() : _context = ChromiumWebView.createContext() {
    _tabsActionController.stream.listen(_handleAction);
  }

  void dispose() {
    for (final tab in _tabsList) {
      tab.dispose();
    }
    _context.ctrl.close();
    _tabsActionController.close();
  }

  Future<void> _handleAction(TabsAction action) async {
    switch (action.op) {
      case TabsActionType.newTab:
        final tab = WebPageBloc(context: _context);
        _tabsList.add(tab);
        tabs.value = UnmodifiableListView<WebPageBloc>(_tabsList);
        currentTab.value = tab;
        break;
      case TabsActionType.focusTab:
        final FocusTabAction focusTab = action;
        currentTab.value = focusTab.tab;
        break;
    }
  }
}
