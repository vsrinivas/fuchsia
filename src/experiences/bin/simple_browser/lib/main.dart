// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:keyboard_shortcuts/keyboard_shortcuts.dart';

import 'app.dart';
import 'create_web_context.dart';
import 'src/blocs/tabs_bloc.dart';
import 'src/blocs/webpage_bloc.dart';
import 'src/models/app_model.dart';
import 'src/models/tabs_action.dart';
import 'src/services/simple_browser_web_service.dart';
import 'src/utils/browser_shortcuts.dart';
import 'src/utils/tld_checker.dart';

void main() {
  setupLogger(name: 'Browser');
  final _context = createWebContext();
  TldChecker().prefetchTlds();

  // Bind |tabsBloc| here so that it can be referenced in the TabsBloc
  // constructor arguments.
  TabsBloc<WebPageBloc> tabsBloc;

  tabsBloc = TabsBloc(
    tabFactory: () {
      SimpleBrowserWebService webService = SimpleBrowserWebService(
        context: _context,
        popupHandler: (tab) => tabsBloc.request.add(
          AddTabAction<WebPageBloc>(tab: tab),
        ),
      );
      return WebPageBloc(
        webService: webService,
      );
    },
    disposeTab: (tab) {
      tab.dispose();
    },
  );

  tabsBloc.request.add(NewTabAction());

  KeyboardShortcuts kShortcuts =
      BrowserShortcuts(tabsBloc: tabsBloc).activateShortcuts();

  final appModel = AppModel.fromStartupContext(
    tabsBloc: tabsBloc,
    keyboardShortcuts: kShortcuts,
  );

  runApp(App(appModel));
}
