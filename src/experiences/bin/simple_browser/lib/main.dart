// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_modular/module.dart' as modular;
import 'package:simple_browser/src/models/tabs_action.dart';
import 'package:simple_browser/src/models/webpage_action.dart';
import 'app.dart';
import 'src/blocs/tabs_bloc.dart';

class RootIntentHandler extends modular.IntentHandler {
  final TabsBloc tabsBloc;
  RootIntentHandler(this.tabsBloc);

  @override
  void handleIntent(modular.Intent intent) {
    /// if there are no tabs, add one
    /// otherwise add a new one only if the current tabs isn't a "New Tab"
    if (tabsBloc.tabs.value.isEmpty ||
        tabsBloc.currentTab.value.url.value.isNotEmpty) {
      tabsBloc.request.add(NewTabAction());
    }
    if (intent.action == 'NavigateToUrl') {
      intent.getEntity(name: 'url', type: 'string').getData().then((bytes) {
        final url = utf8.decode(bytes);
        tabsBloc.currentTab.value.request.add(NavigateToAction(url: url));
      });
    }
  }
}

void main() {
  setupLogger(name: 'Browser');
  final tabsBloc = TabsBloc();
  modular.Module().registerIntentHandler(RootIntentHandler(tabsBloc));
  runApp(App(tabsBloc: tabsBloc));
}
