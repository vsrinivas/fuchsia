// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert' show json;

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_ui_input3/fidl_async.dart'
    show KeyMeaning, NonPrintableKey;
import 'package:fidl_fuchsia_ui_shortcut2/fidl_async.dart' as ui_shortcut;
import 'package:fidl_fuchsia_ui_views/fidl_async.dart' show ViewRef;
import 'package:keyboard_shortcuts/keyboard_shortcuts.dart';
import 'package:mockito/mockito.dart';
import 'package:test/test.dart';
import 'package:zircon/zircon.dart';

void main() async {
  test('Create KeyboardShortcut', () {
    final registry = MockRegistry();
    final listenerBinding = MockListenerBinding();

    final shortcuts = KeyboardShortcuts(
      registry: registry,
      actions: {},
      bindings: '{}',
      listenerBinding: listenerBinding,
      viewRef: ViewRef(reference: MockEventPair()),
    );

    verify(registry.setView(any, any)).called(1);
    expect(verify(listenerBinding.wrap(captureAny)).captured.first, shortcuts);
  });

  KeyMeaning cpKey(int codepoint) {
    return KeyMeaning.withCodepoint(codepoint);
  }

  KeyMeaning npKey(NonPrintableKey key) {
    return KeyMeaning.withNonPrintableKey(key);
  }

  test('Register shortcuts', () {
    final registry = MockRegistry();
    final shortcuts = KeyboardShortcuts(
      registry: registry,
      actions: {'cancel': () {}},
      bindings: json.encode(<String, dynamic>{
        'cancel': [
          {
            'shortcut': 'escape',
            'enabled': true,
          }
        ],
      }),
      listenerBinding: MockListenerBinding(),
    );

    verify(registry.registerShortcut(any)).called(1);
    expect(shortcuts.shortcuts.length, 1);
    expect(
        shortcuts.shortcuts.first.keyMeanings, [npKey(NonPrintableKey.escape)]);
  });

  test('Modifier keys produce a shortcut', () {
    final registry = MockRegistry();
    final shortcuts = KeyboardShortcuts(
      registry: registry,
      actions: {'cancel': () {}},
      bindings: json.encode(<String, dynamic>{
        'cancel': [
          {
            'shortcut': 'alt + escape',
            'chord': 'Escape + Alt',
            'description': 'Cancel Operation'
          }
        ],
      }),
      listenerBinding: MockListenerBinding(),
    );

    verify(registry.registerShortcut(any)).called(1);
    expect(shortcuts.shortcuts.length, 1);
    expect(
      RegExp(r'Escape\ \+\ Alt').allMatches(shortcuts.helpText()).length,
      1,
    );
  });

  test('meta key shortcuts', () {
    final registry = MockRegistry();
    final shortcuts = KeyboardShortcuts(
      registry: registry,
      actions: {'cancel': () {}, 'overview': () {}},
      bindings: json.encode(<String, dynamic>{
        'cancel': [
          {
            'shortcut': 'meta',
            'enabled': true,
          }
        ],
        'overview': [
          {
            'shortcut': 'escape',
            'enabled': true,
            'modifier': 'meta',
          }
        ],
      }),
      listenerBinding: MockListenerBinding(),
    );

    verify(registry.registerShortcut(any)).called(2);
    expect(shortcuts.shortcuts.length, 2);
    expect(shortcuts.shortcuts[0].keyMeanings, [npKey(NonPrintableKey.meta)]);
    expect(shortcuts.shortcuts[1].keyMeanings, [npKey(NonPrintableKey.escape)]);
  });

  test('modifiers expand to required keys', () {
    final registry = MockRegistry();
    final shortcuts = KeyboardShortcuts(
      registry: registry,
      actions: {'cancel': () {}},
      bindings: json.encode(<String, dynamic>{
        'cancel': [
          {
            'shortcut': 'control + shift + tab',
            'enabled': true,
          }
        ],
      }),
      listenerBinding: MockListenerBinding(),
    );

    verify(registry.registerShortcut(any)).called(1);
    expect(shortcuts.shortcuts.length, 1);
    expect(shortcuts.shortcuts[0].keyMeanings, [
      npKey(NonPrintableKey.control),
      npKey(NonPrintableKey.shift),
      npKey(NonPrintableKey.tab)
    ]);
  });

  test('special key names are recognized correctly', () {
    final registry = MockRegistry();
    final shortcuts = KeyboardShortcuts(
      registry: registry,
      actions: {'cancel': () {}},
      bindings: json.encode(<String, dynamic>{
        'cancel': [
          {
            // We allow having more than a single printable character, but that
            // doesn't make it a good idea to use.
            'shortcut': 'altGr + space + plus + slash',
            'enabled': true,
          }
        ],
      }),
      listenerBinding: MockListenerBinding(),
    );

    verify(registry.registerShortcut(any)).called(1);
    expect(shortcuts.shortcuts.length, 1);
    expect(shortcuts.shortcuts[0].keyMeanings, [
      npKey(NonPrintableKey.altGraph),
      cpKey(' '.codeUnitAt(0)),
      cpKey('+'.codeUnitAt(0)),
      cpKey('/'.codeUnitAt(0)),
    ]);
  });

  test('keyboard keys are registered correctly', () {
    final registry = MockRegistry();
    final shortcuts = KeyboardShortcuts(
      registry: registry,
      actions: {'cancel': () {}},
      bindings: json.encode(<String, dynamic>{
        'cancel': [
          {
            'shortcut': 'control + shift + a',
            'enabled': true,
          },
          {
            'shortcut': 'control + shift + A',
            'enabled': true,
          },
          {
            // Fancy!
            'shortcut': 'control + alt + ш',
            'enabled': true,
          }
        ],
      }),
      listenerBinding: MockListenerBinding(),
    );

    verify(registry.registerShortcut(any)).called(3);
    expect(shortcuts.shortcuts.length, 3);
    final expectedSame = [
      npKey(NonPrintableKey.control),
      npKey(NonPrintableKey.shift),
      cpKey('a'.codeUnitAt(0))
    ];
    // The letter case does not matter.
    expect(shortcuts.shortcuts[0].keyMeanings, expectedSame);
    expect(shortcuts.shortcuts[1].keyMeanings, expectedSame);
    expect(shortcuts.shortcuts[2].keyMeanings, [
      npKey(NonPrintableKey.control),
      npKey(NonPrintableKey.alt),
      cpKey('ш'.codeUnitAt(0))
    ]);
  });

  test('malformed key', () {
    final registry = MockRegistry();
    expect(
        () => KeyboardShortcuts(
              registry: registry,
              actions: {'cancel': () {}},
              bindings: json.encode(<String, dynamic>{
                'cancel': [
                  {
                    'shortcut': 'control + shift + xxx_not_a_key',
                    'enabled': true,
                  }
                ],
              }),
              listenerBinding: MockListenerBinding(),
            ),
        throwsException);
  });

  test('Invoke shortcuts', () {
    bool invoked = false;

    final shortcuts = KeyboardShortcuts(
      registry: MockRegistry(),
      actions: {'cancel': () => invoked = true},
      bindings: json.encode(<String, dynamic>{
        'cancel': [
          {
            'shortcut': 'escape',
            'enabled': true,
          }
        ],
      }),
      listenerBinding: MockListenerBinding(),
    );

    shortcuts.onShortcut(shortcuts.shortcuts.first.id);

    expect(invoked, true);
  });
}

// Mock classes.
class MockRegistry extends Mock implements ui_shortcut.Registry {
  @override
  Future<void> setView(
          ViewRef? viewRef, InterfaceHandle<ui_shortcut.Listener>? listener) =>
      super.noSuchMethod(Invocation.method(#setView, [viewRef, listener]));
  @override
  Future<void> registerShortcut(ui_shortcut.Shortcut? shortcut) =>
      super.noSuchMethod(Invocation.method(#registerShortcut, [shortcut]));
}

class MockListenerBinding extends Mock implements ui_shortcut.ListenerBinding {
  @override
  InterfaceHandle<ui_shortcut.Listener> wrap(ui_shortcut.Listener? impl) =>
      super.noSuchMethod(Invocation.method(#wrap, [impl]));
}

class MockEventPair extends Mock implements EventPair {}
