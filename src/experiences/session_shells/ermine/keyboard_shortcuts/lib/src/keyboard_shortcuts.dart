// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert' show json;

import 'package:flutter/material.dart' show VoidCallback;
import 'package:meta/meta.dart';

import 'package:fidl_fuchsia_input/fidl_async.dart' show Key;
import 'package:fidl_fuchsia_ui_shortcut/fidl_async.dart' as ui_shortcut
    show Registry, RegistryProxy, Shortcut, Trigger, Listener, ListenerBinding;
import 'package:fidl_fuchsia_ui_views/fidl_async.dart' show ViewRef;
import 'package:fuchsia_services/services.dart' show StartupContext;
import 'package:zircon/zircon.dart' show EventPair, ZX;

/// Listens for keyboard shortcuts and triggers callbacks when they occur.
class KeyboardShortcuts extends ui_shortcut.Listener {
  final ui_shortcut.Registry registry;
  final Map<String, VoidCallback> actions;
  final List<Shortcut> shortcuts;

  ViewRef _viewRef;
  final ui_shortcut.ListenerBinding _listenerBinding;

  KeyboardShortcuts({
    @required this.registry,
    @required this.actions,
    @required String bindings,
    ViewRef viewRef,
    ui_shortcut.ListenerBinding listenerBinding,
  })  : shortcuts = _decodeJsonBindings(bindings, actions),
        _viewRef = viewRef,
        _listenerBinding = listenerBinding ?? ui_shortcut.ListenerBinding() {
    if (_viewRef != null) {
      registry.setView(_viewRef, _listenerBinding.wrap(this));
    }
    shortcuts.forEach(registry.registerShortcut);
  }

  factory KeyboardShortcuts.fromStartupContext(
    StartupContext startupContext, {
    Map<String, VoidCallback> actions,
    String bindings,
  }) {
    final shortcutRegistry = ui_shortcut.RegistryProxy();
    startupContext.incoming.connectToService(shortcutRegistry);
    ViewRef viewRef = ViewRef(
        reference:
            EventPair(startupContext.viewRef).duplicate(ZX.RIGHT_SAME_RIGHTS));
    return KeyboardShortcuts(
      registry: shortcutRegistry,
      actions: actions,
      bindings: bindings,
      viewRef: viewRef,
    );
  }

  void dispose() {
    if (registry is ui_shortcut.RegistryProxy) {
      ui_shortcut.RegistryProxy proxy = registry;
      proxy.ctrl.close();
    }
    shortcuts.clear();
    _listenerBinding.close();
  }

  @override
  Future<bool> onShortcut(int id) async {
    Shortcut shortcut = shortcuts.firstWhere((shortcut) => shortcut.id == id);
    shortcut.onKey();
    return shortcut?.exclusive ?? false;
  }

  /// Returns keyboard binding help text.
  String helpText() {
    final result = <String, List<String>>{};
    for (final binding in shortcuts) {
      if (result.containsKey(binding.description) &&
          !result[binding.description].contains(binding.chord)) {
        result[binding.description].add(binding.chord);
      } else {
        result[binding.description] = [binding.chord];
      }
    }
    final buf = StringBuffer();
    for (final description in result.keys) {
      buf.writeln(description);
      for (final chord in result[description]) {
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
        VoidCallback callback = actions[key];
        return chords
            .whereType<Map>()
            .map((c) {
              return Shortcut.parseJson(
                object: c,
                onKey: callback,
                action: key,
              );
            })
            .expand((c) => c)
            .toList();
      }
      return value;
    });
    if (data is! Map) {
      return [];
    }
    Map<String, dynamic> kvData = data
      ..removeWhere((name, value) =>
          !actions.containsKey(name) ||
          value == null ||
          value is! List ||
          value.isEmpty);
    return kvData
        .map((k, v) => MapEntry<String, List<Shortcut>>(k, v))
        .values
        .expand((c) => c)
        .toList();
  }
}

/// Defines a keyboard shortcut binding.
class Shortcut extends ui_shortcut.Shortcut {
  static int lastId = 0;

  bool exclusive = true;
  String action;
  String chord;
  String description;
  VoidCallback onKey;

  Shortcut({
    Key key3,
    List<Key> keysRequired,
    ui_shortcut.Trigger trigger,
    bool usePriority = true,
    this.exclusive,
    this.onKey,
    this.action,
    this.chord,
    this.description = '',
  }) : super(
            id: ++lastId,
            keysRequired: keysRequired,
            key3: key3,
            usePriority: usePriority,
            trigger: trigger);

  static List<Shortcut> parseJson({
    Map<String, dynamic> object,
    VoidCallback onKey,
    String action,
  }) {
    return _keysRequiredFromArray(object['modifier'])
        .map((keysRequired) => Shortcut(
              key3: Key.$valueOf(object['char']),
              keysRequired: keysRequired.isEmpty ? null : keysRequired,
              trigger: object['char'] == null && object['modifier'] != null
                  ? ui_shortcut.Trigger.keyPressedAndReleased
                  : null,
              exclusive: object['exclusive'] ?? true,
              onKey: onKey,
              action: action,
              chord: object['chord'],
              description: object['description'],
            ))
        .toList();
  }

  @override
  bool operator ==(dynamic other) =>
      other is Shortcut &&
      id == other.id &&
      key3 == other.key3 &&
      keysRequired == other.keysRequired &&
      usePriority == other.usePriority;

  @override
  int get hashCode =>
      id.hashCode ^
      usePriority.hashCode ^
      key3.hashCode ^
      keysRequired.hashCode;

  @override
  String toString() =>
      'id: $id key3: $key3 keysRequired: $keysRequired action: $action';

  static List<List<Key>> _keysRequiredFromArray(String s) {
    List<List<Key>> r = [[]];
    if (s == null) {
      return r;
    }
    var modifiers =
        s.split('+').map((x) => x.trim()).map(_keyVariantsFromString);
    // Convert list of modifier variants to a list of combinations.
    for (var modifierVariant in modifiers) {
      r = r.expand((i) => modifierVariant.map((j) => i + [j])).toList();
    }
    return r;
  }

  static List<Key> _keyVariantsFromString(String s) {
    switch (s) {
      case 'shift':
        return [Key.leftShift, Key.rightShift];
      case 'control':
        return [Key.leftCtrl, Key.rightCtrl];
      case 'alt':
        return [Key.leftAlt, Key.rightAlt];
      case 'meta':
        return [Key.leftMeta, Key.rightMeta];
      default:
        return [Key.$valueOf(s)];
    }
  }
}
