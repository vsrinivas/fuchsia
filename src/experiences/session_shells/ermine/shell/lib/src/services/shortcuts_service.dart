// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:keyboard_shortcuts/keyboard_shortcuts.dart';

/// Defines a service to manage keyboard shortcuts.
class ShortcutsService {
  /// A command handler used by [FlutterDriverExtension] to allow invoking
  /// custom commands during testing.
  static Future<String> Function(String?)? flutterDriverHandler;

  final ViewRef hostViewRef;

  ShortcutsService(this.hostViewRef);

  late final KeyboardShortcuts _keyboardShortcuts;

  Map<String, Set<String>> get keyboardBindings =>
      _keyboardShortcuts.bindingDescription();

  void register(Map<String, dynamic> actions) {
    final file = File('/pkg/data/keyboard_shortcuts.json');
    final bindings = file.readAsStringSync();

    _keyboardShortcuts = KeyboardShortcuts.withViewRef(
      hostViewRef,
      actions: actions.map((k, v) => MapEntry(k, () => v())),
      bindings: bindings,
    );

    // Hook up actions to flutter driver handler.
    flutterDriverHandler = (command) async {
      return actions[command]?.call();
    };
  }

  void dispose() {
    _keyboardShortcuts.dispose();
  }
}
