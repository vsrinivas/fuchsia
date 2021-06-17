// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';
import 'dart:ui';

import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:keyboard_shortcuts/keyboard_shortcuts.dart';

/// Defines a service to manage keyboard shortcuts.
class ShortcutsService {
  final ViewRef hostViewRef;

  ShortcutsService(this.hostViewRef);

  late final KeyboardShortcuts _keyboardShortcuts;

  Map<String, Set<String>> get keyboardBindings =>
      _keyboardShortcuts.bindingDescription();

  void register(Map<String, VoidCallback> actions) {
    final file = File('/pkg/data/keyboard_shortcuts.json');
    final bindings = file.readAsStringSync();

    _keyboardShortcuts = KeyboardShortcuts.withViewRef(
      hostViewRef,
      actions: actions,
      bindings: bindings,
    );

    // Hook up actions to flutter driver handler.
    if (TestDefaultBinaryMessengerBinding.instance != null) {
      MethodChannel('flutter_driver/handler')
          .setMockMethodCallHandler((call) async {
        actions[call.method]?.call();
      });
    }
  }

  void dispose() {
    _keyboardShortcuts.dispose();
  }
}
