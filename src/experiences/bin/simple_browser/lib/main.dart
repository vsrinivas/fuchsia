// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: unused_import
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';
import 'package:fuchsia_services/services.dart';

import 'app.dart';
import 'create_web_context.dart';
import 'src/blocs/tabs_bloc.dart';
import 'src/blocs/webpage_bloc.dart';
import 'src/models/app_model.dart';
import 'src/models/tabs_action.dart';
import 'src/models/webpage_action.dart';
import 'src/services/simple_browser_web_service.dart';
import 'src/utils/tld_checker.dart';

final _handler = MethodChannel('flutter_driver/handler');

void main() {
  setupLogger(name: 'simple_browser');
  final _context = createWebContext();
  TldChecker().prefetchTlds();
  ComponentContext.createAndServe();

  // Loads MaterialIcons-Regular.otf
  File file = File('/pkg/data/MaterialIcons-Regular.otf');
  if (file.existsSync()) {
    FontLoader('MaterialIcons')
      ..addFont(() async {
        return file.readAsBytesSync().buffer.asByteData();
      }())
      ..load();
  }

  // Binds |tabsBloc| here so that it can be referenced in the TabsBloc
  // constructor arguments.
  late TabsBloc tabsBloc;

  tabsBloc = TabsBloc(
    tabFactory: () {
      SimpleBrowserWebService webService = SimpleBrowserWebService(
        context: _context,
        popupHandler: (tab) => tabsBloc.request.add(
          AddTabAction(tab: tab),
        ),
        onLoaded: () => tabsBloc.currentTab!.request.add(SetFocusAction()),
      );

      // Enables the web console log only for debug.
      assert(() {
        webService.enableConsoleLog();
        return true;
      }());

      return WebPageBloc(
        webService: webService,
      );
    },
    disposeTab: (tab) {
      tab.dispose();
    },
  );

  tabsBloc.request.add(NewTabAction());

  final appModel = AppModel.fromStartupContext(
    tabsBloc: tabsBloc,
  );

  runApp(App(appModel));

  // Moves focus to the webview whenever the browser gets focused.
  FocusState.instance.stream().listen(appModel.onFocus);

  // This call is used only when flutter driver is enabled.
  if (TestDefaultBinaryMessengerBinding.instance != null) {
    _handler.setMockMethodCallHandler((call) async {
      final url = call.method;
      tabsBloc.currentTab!.request.add(NavigateToAction(url: url));
      log.info('Navigate to $url...');
    });
  }
}
