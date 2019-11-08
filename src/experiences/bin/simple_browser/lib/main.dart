// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_modular/module.dart' as modular;
import 'package:simple_browser/src/models/tabs_action.dart';
import 'package:simple_browser/src/models/webpage_action.dart';
import 'package:webview/webview.dart';
import 'app.dart';
import 'src/blocs/tabs_bloc.dart';
import 'src/blocs/webpage_bloc.dart';

class RootIntentHandler extends modular.IntentHandler {
  final TabsBloc<WebPageBloc> tabsBloc;
  RootIntentHandler(this.tabsBloc);

  @override
  void handleIntent(modular.Intent intent) {
    /// if there are no tabs, add one
    /// otherwise add a new one only if the current tabs isn't a "New Tab"
    if (tabsBloc.tabs.isEmpty || tabsBloc.currentTab.url.isNotEmpty) {
      tabsBloc.request.add(NewTabAction<WebPageBloc>());
    }
    if (intent.action == 'NavigateToUrl') {
      intent.getEntity(name: 'url', type: 'string').getData().then((bytes) {
        final url = utf8.decode(bytes);
        tabsBloc.currentTab.request.add(NavigateToAction(url: url));
      });
    }
  }
}

void main() {
  setupLogger(name: 'Browser');
  final _context = ChromiumWebView.createContext();

  // Bind |tabsBloc| here so that it can be referenced in the TabsBloc
  // constructor arguments.
  TabsBloc<WebPageBloc> tabsBloc;
  tabsBloc = TabsBloc(
    tabFactory: () => WebPageBloc(
        context: _context,
        popupHandler: (tab) =>
            tabsBloc.request.add(AddTabAction<WebPageBloc>(tab: tab))),
    disposeTab: (tab) {
      tab.dispose();
    },
  );
  modular.Module().registerIntentHandler(RootIntentHandler(tabsBloc));
  runApp(App(tabsBloc: tabsBloc));
}
