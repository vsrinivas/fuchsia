// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io';

import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:internationalization/strings.dart';
import 'package:keyboard_shortcuts/keyboard_shortcuts.dart';

/// Defines a service to manage keyboard shortcuts.
class ShortcutsService {
  /// A command handler used by [FlutterDriverExtension] to allow invoking
  /// custom commands during testing.
  static Future<String> Function(String?)? flutterDriverHandler;

  final ViewRef hostViewRef;

  /// Returns the last shortcut received by the application.
  String lastShortcutAction = '';

  ShortcutsService(this.hostViewRef);

  late final KeyboardShortcuts _keyboardShortcuts;

  Map<String, Set<String>> get keyboardBindings =>
      _keyboardShortcuts.bindingDescription();

  void register(Map<String, dynamic> actions) {
    final file = File('/pkg/data/keyboard_shortcuts.json');
    final bindings = _getLocalizedBindings(file.readAsStringSync());

    _keyboardShortcuts = KeyboardShortcuts.withViewRef(
      hostViewRef,
      actions: actions.map((k, v) => MapEntry(k, () {
            log.info('Received shortcut action: $k');
            lastShortcutAction = k;
            v();
          })),
      bindings: bindings,
    );

    // Hook up actions to flutter driver handler.
    flutterDriverHandler = (command) async {
      log.info('Received flutter driver command: $command');
      return actions[command]?.call();
    };
  }

  void dispose() {
    _keyboardShortcuts.dispose();
  }

  String _getLocalizedBindings(String bindings) {
    Map<String, dynamic> shortcutSpec = jsonDecode(bindings);
    for (final entry in shortcutSpec.entries) {
      final shortcuts = entry.value;
      for (Map<String, dynamic> shortcut in shortcuts) {
        if (shortcut.containsKey('localizedDescription')) {
          final localizedKey = shortcut['localizedDescription'];
          final message = Strings.lookup(localizedKey);
          shortcut['description'] = message ?? shortcut['description'] ?? '';
        }
      }
    }
    return jsonEncode(shortcutSpec);
  }
}
