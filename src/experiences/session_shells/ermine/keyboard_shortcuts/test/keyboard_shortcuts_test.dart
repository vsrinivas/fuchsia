// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert' show json;

import 'package:fidl_fuchsia_input/fidl_async.dart' show Key;
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
    expect(shortcuts.shortcuts.first.key3, Key.escape);
  });

  test('meta key shortcuts', () {
    final registry = MockRegistry();
    final shortcuts = KeyboardShortcuts(
      registry: registry,
      actions: {'cancel': () {}, 'overview': () {}},
      bindings: json.encode(<String, dynamic>{
        'cancel': [
          {
            'char': 'leftMeta',
            'enabled': true,
          }
        ],
        'overview': [
          {
            'char': 'escape',
            'enabled': true,
            'modifier': 'meta',
          }
        ],
      }),
      listenerBinding: MockListenerBinding(),
      viewRef: MockViewRef(),
    );

    verify(registry.registerShortcut(any)).called(3);
    expect(shortcuts.shortcuts.length, 3);
    expect(shortcuts.shortcuts[0].key3, Key.leftMeta);
    expect(shortcuts.shortcuts[1].key3, Key.escape);
    expect(shortcuts.shortcuts[2].key3, Key.escape);
    expect(shortcuts.shortcuts[0].keysRequired, null);
    expect(shortcuts.shortcuts[1].keysRequired, [Key.leftMeta]);
    expect(shortcuts.shortcuts[2].keysRequired, [Key.rightMeta]);
  });

  test('modifiers expand to required keys', () {
    final registry = MockRegistry();
    final shortcuts = KeyboardShortcuts(
      registry: registry,
      actions: {'cancel': () {}},
      bindings: json.encode(<String, dynamic>{
        'cancel': [
          {
            'char': 'tab',
            'enabled': true,
            'modifier': 'control + shift',
          }
        ],
      }),
      listenerBinding: MockListenerBinding(),
      viewRef: MockViewRef(),
    );

    verify(registry.registerShortcut(any)).called(4);
    expect(shortcuts.shortcuts.length, 4);
    expect(shortcuts.shortcuts[0].key3, Key.tab);
    expect(shortcuts.shortcuts[1].key3, Key.tab);
    expect(shortcuts.shortcuts[2].key3, Key.tab);
    expect(shortcuts.shortcuts[3].key3, Key.tab);
    expect(shortcuts.shortcuts[0].keysRequired, [Key.leftCtrl, Key.leftShift]);
    expect(shortcuts.shortcuts[1].keysRequired, [Key.leftCtrl, Key.rightShift]);
    expect(shortcuts.shortcuts[2].keysRequired, [Key.rightCtrl, Key.leftShift]);
    expect(
        shortcuts.shortcuts[3].keysRequired, [Key.rightCtrl, Key.rightShift]);
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
