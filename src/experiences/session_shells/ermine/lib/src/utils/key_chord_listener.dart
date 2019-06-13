// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert' show ascii, json;
import 'dart:ui' show VoidCallback;

import 'package:flutter/material.dart' show VoidCallback;
import 'package:meta/meta.dart';

import 'package:fidl_fuchsia_ui_policy/fidl_async.dart'
    show
        KeyboardCaptureListenerHack,
        KeyboardCaptureListenerHackBinding,
        Presentation;
import 'package:fidl_fuchsia_ui_input/fidl_async.dart';

/// Listens for key chords and triggers its callbacks when they occur.
class KeyChordListener extends KeyboardCaptureListenerHack {
  final Presentation presentation;
  final Map<String, VoidCallback> actions;
  final List<KeyChordBinding> bindings;

  final _keyListenerBindings =
      <KeyChordBinding, KeyboardCaptureListenerHackBinding>{};
  KeyChordListener({
    @required this.presentation,
    this.actions,
    String bindings,
  }) : bindings = _decodeJsonBindings(bindings, actions);

  /// Starts listening to key chords.
  void listen() {
    for (final chord in bindings) {
      final binding = KeyboardCaptureListenerHackBinding();
      presentation.captureKeyboardEventHack(chord, binding.wrap(this));
      _keyListenerBindings[chord] = binding;
    }
  }

  /// Closes all key shortcut bindings.
  void close() {
    for (final binding in _keyListenerBindings.values) {
      binding.close();
    }
    _keyListenerBindings.clear();
  }

  /// Adds a binding for a keyboard shortcut.
  void add(KeyChordBinding chord) {
    Timer(Duration(microseconds: 0), () {
      chord.onKey = actions[chord.action];
      final binding = KeyboardCaptureListenerHackBinding();
      presentation.captureKeyboardEventHack(chord, binding.wrap(this));
      _keyListenerBindings[chord] = binding;
      bindings.add(chord);
    });
  }

  /// Releases the binding for the keyboard shortcut.
  void release(KeyChordBinding chord) {
    Timer(Duration(microseconds: 0), () {
      _keyListenerBindings[chord]?.close();
      _keyListenerBindings.remove(chord);
      bindings.remove(chord);
    });
  }

  /// |KeyboardCaptureListenerHack|.
  @override
  Future<void> onEvent(KeyboardEvent event) async {
    for (final chord in bindings) {
      if (chord.codePoint > 0 && chord.codePoint != event.codePoint ||
          chord.modifiers > 0 && chord.modifiers != event.modifiers ||
          chord.hidUsage > 0 && chord.hidUsage != event.hidUsage) {
        continue;
      }
      chord.onKey?.call();
    }
  }

  static List<KeyChordBinding> _decodeJsonBindings(
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
            .map((c) => KeyChordBinding.fromJSON(
                  object: c,
                  onKey: callback,
                  action: key,
                ))
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
        .map((k, v) => MapEntry<String, List<KeyChordBinding>>(k, v))
        .values
        .expand((c) => c)
        .toList();
  }
}

/// Defines a keyboard shortcut binding.
class KeyChordBinding extends KeyboardEvent {
  String action;
  VoidCallback onKey;

  KeyChordBinding({
    int eventTime = 0,
    int deviceId = 0,
    KeyboardEventPhase phase = KeyboardEventPhase.pressed,
    int hidUsage = 0,
    int codePoint = 0,
    String char,
    int modifiers = 0,
    this.onKey,
    this.action,
  }) : super(
          eventTime: eventTime,
          deviceId: deviceId,
          phase: phase,
          hidUsage: hidUsage,
          codePoint: char != null ? ascii.encode(char).first : codePoint,
          modifiers: modifiers,
        );

  KeyChordBinding.fromJSON({
    Map<String, dynamic> object,
    this.onKey,
    this.action,
  }) : super(
            eventTime: 0,
            deviceId: 0,
            phase: KeyboardEventPhase.pressed,
            hidUsage: object['hidUsage'] ?? 0,
            codePoint: object['char'] != null
                ? ascii.encode(object['char'].toString()).first ?? 0
                : object['codePoint'] ?? 0,
            modifiers: object['modifier'] ?? 0);

  @override
  bool operator ==(dynamic other) =>
      other is KeyChordBinding &&
      action == other.action &&
      phase == other.phase &&
      hidUsage == other.hidUsage &&
      codePoint == other.codePoint &&
      modifiers == other.modifiers;

  @override
  int get hashCode =>
      action.hashCode ^
      phase.hashCode ^
      hidUsage.hashCode ^
      codePoint.hashCode ^
      modifiers.hashCode;
}
