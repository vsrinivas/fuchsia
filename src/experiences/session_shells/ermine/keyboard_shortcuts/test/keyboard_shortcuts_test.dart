// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert' show json;

import 'package:fidl_fuchsia_ui_input2/fidl_async.dart';
import 'package:fidl_fuchsia_ui_shortcut/fidl_async.dart' as ui_shortcut;
import 'package:fidl_fuchsia_ui_views/fidl_async.dart' show ViewRef;

import 'package:test/test.dart';
import 'package:mockito/mockito.dart';

// ignore_for_file: implementation_imports
import 'package:keyboard_shortcuts/keyboard_shortcuts.dart';

void main() async {
  test('Create KeyboardShortuc', () {
    final registry = MockRegistry();
    final listenerBinding = MockListenerBinding();

    final shortcuts = KeyboardShortcuts(
      registry: registry,
      actions: {},
      bindings: '{}',
      listenerBinding: listenerBinding,
      viewRef: MockViewRef(),
    );

    verify(registry.setView(any, any)).called(1);
    expect(verify(listenerBinding.wrap(captureAny)).captured.first, shortcuts);
  });

  test('Register shortcuts', () {
    final registry = MockRegistry();
    final shortcuts = KeyboardShortcuts(
      registry: registry,
      actions: {'cancel': () {}},
      bindings: json.encode(<String, dynamic>{
        'cancel': [
          {
            'char': 'escape',
            'enabled': true,
          }
        ],
      }),
      listenerBinding: MockListenerBinding(),
      viewRef: MockViewRef(),
    );

    verify(registry.registerShortcut(any)).called(1);
    expect(shortcuts.shortcuts.length, 1);
    expect(shortcuts.shortcuts.first.key, Key.escape);
  });

  test('Invoke shortcuts', () {
    bool invoked = false;

    final shortcuts = KeyboardShortcuts(
      registry: MockRegistry(),
      actions: {'cancel': () => invoked = true},
      bindings: json.encode(<String, dynamic>{
        'cancel': [
          {
            'char': 'escape',
            'enabled': true,
          }
        ],
      }),
      listenerBinding: MockListenerBinding(),
      viewRef: MockViewRef(),
    );

    shortcuts.onShortcut(shortcuts.shortcuts.first.id);

    expect(invoked, true);
  });
}

// Mock classes.
class MockRegistry extends Mock implements ui_shortcut.Registry {}

class MockListenerBinding extends Mock implements ui_shortcut.ListenerBinding {}

class MockViewRef extends Mock implements ViewRef {}
