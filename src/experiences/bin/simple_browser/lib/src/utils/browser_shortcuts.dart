// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import 'dart:io';

import 'package:fidl_fuchsia_ui_shortcut/fidl_async.dart' as ui_shortcut
    show RegistryProxy;
import 'package:fidl_fuchsia_ui_views/fidl_async.dart' show ViewRef;
import 'package:flutter/material.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart';
import 'package:keyboard_shortcuts/keyboard_shortcuts.dart';
import 'package:simple_browser/src/blocs/tabs_bloc.dart';

import '../models/tabs_action.dart';
import '../models/webpage_action.dart';

const path = '/pkg/data/keyboard_shortcuts.json';

class BrowserShortcuts {
  final TabsBloc tabsBloc;
  late ui_shortcut.RegistryProxy registryProxy;
  late Map<String, VoidCallback> actions;

  factory BrowserShortcuts({
    required TabsBloc tabsBloc,
    ui_shortcut.RegistryProxy? shortcutRegistry,
    Map<String, VoidCallback>? actions,
  }) {
    if (shortcutRegistry == null) {
      return BrowserShortcuts._fromStartupContext(
        tabsBloc: tabsBloc,
        actionMap: actions,
      );
    }
    return BrowserShortcuts._afterStartupContext(
      tabsBloc: tabsBloc,
      actionMap: actions,
    );
  }

  BrowserShortcuts._fromStartupContext({
    required this.tabsBloc,
    Map<String, VoidCallback>? actionMap,
  }) {
    registryProxy = ui_shortcut.RegistryProxy();
    Incoming.fromSvcPath()
      ..connectToService(registryProxy)
      ..close();

    actions = actionMap ?? defaultActions();
  }

  BrowserShortcuts._afterStartupContext({
    required this.tabsBloc,
    Map<String, VoidCallback>? actionMap,
  }) {
    actions = actionMap ?? defaultActions();
  }

  KeyboardShortcuts? activateShortcuts(ViewRef viewRef) {
    File file = File(path);
    file.readAsString().then((bindings) {
      return KeyboardShortcuts(
        registry: registryProxy,
        actions: actions,
        bindings: bindings,
        viewRef: viewRef,
      );
    }).catchError((err) {
      log.shout('$err: Failed to activate keyboard shortcuts.');
    });
    return null;
  }

  Map<String, VoidCallback> defaultActions() {
    return {
      'newTab': _newTab,
      'closeTab': _closeTab,
      'goBack': _goBack,
      'goForward': _goForward,
      'refresh': _refresh,
      'previousTab': _previousTab,
      'nextTab': _nextTab,
    };
  }

  void _newTab() => tabsBloc.request.add(NewTabAction());

  void _closeTab() {
    if (tabsBloc.isOnlyTab) {
      return;
    }
    tabsBloc.request.add(RemoveTabAction(tab: tabsBloc.currentTab!));
  }

  void _goBack() {
    if (tabsBloc.currentTab!.backState) {
      tabsBloc.currentTab!.request.add(GoBackAction());
      return;
    }
    return;
  }

  void _goForward() {
    if (tabsBloc.currentTab!.forwardState) {
      tabsBloc.currentTab!.request.add(GoForwardAction());
      return;
    }
    return;
  }

  void _refresh() => tabsBloc.currentTab!.request.add(RefreshAction());

  void _previousTab() {
    if (tabsBloc.isOnlyTab || tabsBloc.previousTab == null) {
      return;
    }
    tabsBloc.request.add(FocusTabAction(tab: tabsBloc.previousTab!));
  }

  void _nextTab() {
    if (tabsBloc.isOnlyTab || tabsBloc.nextTab == null) {
      return;
    }
    tabsBloc.request.add(FocusTabAction(tab: tabsBloc.nextTab!));
  }
}
