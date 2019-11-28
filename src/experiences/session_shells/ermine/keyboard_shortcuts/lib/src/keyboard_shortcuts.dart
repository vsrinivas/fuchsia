// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert' show json;

import 'package:flutter/material.dart' show VoidCallback;
import 'package:meta/meta.dart';

import 'package:fidl_fuchsia_ui_input2/fidl_async.dart';
import 'package:fidl_fuchsia_ui_shortcut/fidl_async.dart' as ui_shortcut
    show Registry, Shortcut, Trigger, Listener, ListenerBinding;
import 'package:fidl_fuchsia_ui_views/fidl_async.dart' show ViewRef;
import 'package:zircon/zircon.dart' show EventPairPair;

/// Listens for keyboard shortcuts and triggers callbacks when they occur.
class KeyboardShortcuts extends ui_shortcut.Listener {
  final ui_shortcut.Registry registry;
  final Map<String, VoidCallback> actions;
  final List<Shortcut> shortcuts;

  // final _viewRef = ViewRef(reference: EventPairPair().first);
  final ViewRef _viewRef;
  final ui_shortcut.ListenerBinding _listenerBinding;

  KeyboardShortcuts({
    @required this.registry,
    @required this.actions,
    @required String bindings,
    ui_shortcut.ListenerBinding listenerBinding,
    ViewRef viewRef,
  })  : shortcuts = _decodeJsonBindings(bindings, actions),
        _viewRef = viewRef ?? ViewRef(reference: EventPairPair().first),
        _listenerBinding = listenerBinding ?? ui_shortcut.ListenerBinding() {
    registry.setView(_viewRef, _listenerBinding.wrap(this));
    shortcuts.forEach(registry.registerShortcut);
  }

  void dispose() {
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
      if (result.containsKey(binding.description)) {
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
        return chords.whereType<Map>().map((c) {
          return Shortcut.fromJSON(
            object: c,
            onKey: callback,
            action: key,
          );
        }).toList();
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
    Key key,
    Modifiers modifiers,
    bool usePriority = true,
    this.exclusive,
    this.onKey,
    this.action,
    this.description = '',
  }) : super(
            id: ++lastId,
            modifiers: modifiers,
            key: key,
            usePriority: usePriority);

  Shortcut.fromJSON({
    Map<String, dynamic> object,
    this.onKey,
    this.action,
  })  : chord = object['chord'],
        description = object['description'],
        exclusive = object['exclusive'] ?? true,
        super(
            id: ++lastId,
            key: Key.$valueOf(object['char']),
            trigger: object['char'] == null && object['modifier'] != null
                ? ui_shortcut.Trigger.keyPressedAndReleased
                : null,
            modifiers: _modifiersFromArray(object['modifier']));

  @override
  bool operator ==(dynamic other) =>
      other is Shortcut &&
      id == other.id &&
      modifiers == other.modifiers &&
      key == other.key &&
      usePriority == other.usePriority;

  @override
  int get hashCode =>
      id.hashCode ^ modifiers.hashCode ^ key.hashCode ^ usePriority.hashCode;

  @override
  String toString() =>
      'id: $id key: $key modifiers: $modifiers action: $action';

  static Modifiers _modifiersFromArray(String s) {
    if (s == null) {
      return null;
    }
    return s
        .split('+')
        .map((x) => x.trim())
        .map(_modifierFromString)
        .reduce((prev, next) => prev | next);
  }

  static Modifiers _modifierFromString(String s) {
    switch (s) {
      case 'shift':
        return Modifiers.shift;
      case 'leftShift':
        return Modifiers.leftShift;
      case 'rightShift':
        return Modifiers.rightShift;
      case 'control':
        return Modifiers.control;
      case 'leftControl':
        return Modifiers.leftControl;
      case 'rightControl':
        return Modifiers.rightControl;
      case 'alt':
        return Modifiers.alt;
      case 'leftAlt':
        return Modifiers.leftAlt;
      case 'rightAlt':
        return Modifiers.rightAlt;
      case 'meta':
        return Modifiers.meta;
      case 'leftMeta':
        return Modifiers.leftMeta;
      case 'rightMeta':
        return Modifiers.rightMeta;
      case 'capsLock':
        return Modifiers.capsLock;
      case 'numLock':
        return Modifiers.numLock;
      case 'scrollLock':
        return Modifiers.scrollLock;
      default:
        return Modifiers.$none;
    }
  }
}
