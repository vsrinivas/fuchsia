// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert' show json;

import 'package:fidl_fuchsia_ui_shortcut2/fidl_async.dart' as ui_shortcut
    show
        Registry,
        RegistryProxy,
        Options,
        Handled,
        Shortcut,
        Listener,
        ListenerBinding;
import 'package:fidl_fuchsia_ui_input3/fidl_async.dart';
import 'package:fidl_fuchsia_ui_views/fidl_async.dart' show ViewRef;
import 'package:flutter/material.dart' show VoidCallback;
import 'package:fuchsia_services/services.dart' show Incoming;

/// Listens for keyboard shortcuts and triggers callbacks when they occur.
class KeyboardShortcuts extends ui_shortcut.Listener {
  final ui_shortcut.Registry registry;
  final Map<String, VoidCallback> actions;
  final List<Shortcut> shortcuts;

  ViewRef? _viewRef;
  final ui_shortcut.ListenerBinding _listenerBinding;

  KeyboardShortcuts({
    required this.registry,
    required this.actions,
    required String bindings,
    ViewRef? viewRef,
    ui_shortcut.ListenerBinding? listenerBinding,
  })  : shortcuts = _decodeJsonBindings(bindings, actions),
        _viewRef = viewRef,
        _listenerBinding = listenerBinding ?? ui_shortcut.ListenerBinding() {
    if (_viewRef != null) {
      registry.setView(_viewRef!, _listenerBinding.wrap(this));
    }
    shortcuts.forEach(registry.registerShortcut);
  }

  factory KeyboardShortcuts.withViewRef(
    ViewRef viewRef, {
    required Map<String, VoidCallback> actions,
    required String bindings,
  }) {
    final shortcutRegistry = ui_shortcut.RegistryProxy();
    Incoming.fromSvcPath().connectToService(shortcutRegistry);
    return KeyboardShortcuts(
      registry: shortcutRegistry,
      actions: actions,
      bindings: bindings,
      viewRef: viewRef,
    );
  }

  void dispose() {
    if (registry is ui_shortcut.RegistryProxy) {
      final proxy = registry as ui_shortcut.RegistryProxy;
      proxy.ctrl.close();
    }
    shortcuts.clear();
    _listenerBinding.close();
  }

  @override
  Future<ui_shortcut.Handled> onShortcut(int id) async {
    Shortcut shortcut = shortcuts.firstWhere((shortcut) => shortcut.id == id);
    shortcut.onKey?.call();
    return shortcut.usePriority == true
        ? ui_shortcut.Handled.handled
        : ui_shortcut.Handled.notHandled;
  }

  Map<String, Set<String>> bindingDescription() {
    final result = <String, Set<String>>{};
    for (final binding in shortcuts) {
      if (result.containsKey(binding.description)) {
        result[binding.description]!.add(binding.chord!);
      } else {
        result[binding.description] = {binding.chord!};
      }
    }
    return result;
  }

  /// Returns keyboard binding help text.
  String helpText() {
    final result = bindingDescription();
    final buf = StringBuffer();
    for (final description in result.keys) {
      buf.writeln(description);
      for (final chord in result[description]!) {
        buf
          ..write('          ')
          ..writeln(chord);
      }
    }
    return buf.toString();
  }

  static List<Shortcut> _decodeJsonBindings(
      String bindings, Map<String, VoidCallback> actions) {
    final data = json.decode(bindings, reviver: (key, value) {
      if (actions.containsKey(key)) {
        if (value is! List) {
          return null;
        }

        List<dynamic> chords = value;
        VoidCallback callback = actions[key]!;
        return chords.whereType<Map>().map((c) {
          return Shortcut.parseJson(
            object: c as Map<String, dynamic>,
            onKey: callback,
            action: key.toString(),
          );
        }).toList();
      }
      return value;
    });
    if (data is! Map) {
      return [];
    }
    Map<String, dynamic> kvData = data as Map<String, dynamic>
      ..removeWhere((name, value) =>
          !actions.containsKey(name) ||
          value == null ||
          value is! List ||
          value.isEmpty);
    return kvData
        // TODO(https://fxbug.dev/71711): Figure out why `dart analyze`
        // complains about this.
        // ignore: unnecessary_lambdas
        .map((k, v) => MapEntry<String, List<Shortcut>>(k, v))
        .values
        .expand((c) => c)
        .toList();
  }
}

/// Defines a keyboard shortcut binding.
class Shortcut extends ui_shortcut.Shortcut {
  static int lastId = 0;

  String? action;
  String? chord;
  String description;
  VoidCallback? onKey;
  bool usePriority;

  Shortcut({
    List<KeyMeaning> keyMeanings = const [],
    this.usePriority = true,
    this.onKey,
    this.action,
    this.chord,
    this.description = '',
  }) : super(
            id: ++lastId,
            keyMeanings: keyMeanings,
            options: const ui_shortcut.Options());

  /// Parses shortcut JSON description into a Shortcut object.
  Shortcut.parseJson({
    required Map<String, dynamic> object,
    required VoidCallback onKey,
    required String action,
  }) : this(
          keyMeanings: _keysRequiredFromArray(object['shortcut']),
          onKey: onKey,
          usePriority: object['exclusive'] == true,
          action: action,
          chord: object['chord'],
          description: object['description'],
        );

  @override
  bool operator ==(dynamic other) =>
      other is Shortcut &&
      id == other.id &&
      keyMeanings == other.keyMeanings &&
      usePriority == other.usePriority;

  @override
  int get hashCode => id.hashCode ^ keyMeanings.hashCode ^ usePriority.hashCode;

  /// Turns a descriptive string describing a keyboard shortcut, such as
  /// `ctrl + alt + f` into a list of equivalent key meanings.
  static List<KeyMeaning> _keysRequiredFromArray(String? s) {
    List<KeyMeaning> r = [];
    if (s == null) {
      return r;
    }
    return s
        .split('+')
        .map((x) => x.trim())
        .map(_keyMeaningFromString)
        .toList();
  }

  /// Recovers a KeyMeaning from a string that describes an individual key.
  static KeyMeaning _keyMeaningFromString(String s) {
    // First, try some special names.
    switch (s) {
      case 'ctrl':
        return KeyMeaning.withNonPrintableKey(NonPrintableKey.control);
      case 'alt':
        return KeyMeaning.withNonPrintableKey(NonPrintableKey.alt);
      case 'altGr':
      case 'altgr':
        return KeyMeaning.withNonPrintableKey(NonPrintableKey.altGraph);
      case 'esc':
        return KeyMeaning.withNonPrintableKey(NonPrintableKey.escape);
      case 'space':
        return KeyMeaning.withCodepoint(' '.codeUnitAt(0));
      case 'slash':
        return KeyMeaning.withCodepoint('/'.codeUnitAt(0));
      case 'plus':
        // Our notation uses '+' as key separator, so have this special way
        // to denote a plus.
        return KeyMeaning.withCodepoint('+'.codeUnitAt(0));
      default:
      // fallthrough
    }
    // Then, try known nonprintable keys.
    final NonPrintableKey? maybeNonprintable = NonPrintableKey.$valueOf(s);
    if (maybeNonprintable != null) {
      return KeyMeaning.withNonPrintableKey(maybeNonprintable);
    }
    // Then try 1-code point values.
    if (s.length == 1) {
      return KeyMeaning.withCodepoint(s.toLowerCase().codeUnitAt(0));
    }
    throw Exception('Unsupported key meaning encountered: "$s"');
  }
}
