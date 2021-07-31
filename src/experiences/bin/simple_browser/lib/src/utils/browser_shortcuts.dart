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

const path = '/pkg/data/keyboard_shortcuts.json';

class BrowserShortcuts {
  final TabsBloc tabsBloc;
  late ui_shortcut.RegistryProxy registryProxy;
  final Map<String, VoidCallback> actions;

  factory BrowserShortcuts({
    required TabsBloc tabsBloc,
    required Map<String, VoidCallback> actions,
    ui_shortcut.RegistryProxy? shortcutRegistry,
  }) {
    if (shortcutRegistry == null) {
      return BrowserShortcuts._fromStartupContext(
        tabsBloc: tabsBloc,
        actions: actions,
      );
    }
    return BrowserShortcuts._afterStartupContext(
      tabsBloc: tabsBloc,
      actions: actions,
    );
  }

  BrowserShortcuts._fromStartupContext({
    required this.tabsBloc,
    required this.actions,
  }) {
    registryProxy = ui_shortcut.RegistryProxy();
    Incoming.fromSvcPath()
      ..connectToService(registryProxy)
      ..close();
  }

  BrowserShortcuts._afterStartupContext({
    required this.tabsBloc,
    required this.actions,
  });

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
}
