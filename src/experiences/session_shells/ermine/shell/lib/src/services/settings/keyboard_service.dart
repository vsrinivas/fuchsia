// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:collection/collection.dart';
import 'package:ermine/src/services/settings/task_service.dart';
import 'package:fidl_fuchsia_input/fidl_async.dart';
import 'package:fidl_fuchsia_settings/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart';

/// Defines a [TaskService] to control keyboard mappings.
class KeyboardService implements TaskService {
  late final VoidCallback onChanged;
  final Map<KeymapId, String> _keymapIds = {
    KeymapId.usQwerty: 'usQwerty',
    KeymapId.frAzerty: 'frAzerty',
    KeymapId.usDvorak: 'usDvorak',
    KeymapId.usColemak: 'usColemak'
  };

  KeyboardProxy? _proxy;
  KeyboardSettings? _keyboardSettings;
  StreamSubscription? _keyboardSubscription;
  String _keymap = 'usQwerty';

  KeyboardService();

  String get currentKeymap => _keymap;
  set currentKeymap(String value) {
    assert(_proxy != null, 'KeyboardService not started');

    var newKeymapId =
        _keymapIds.keys.firstWhereOrNull((id) => _keymapIds[id] == value);
    if (newKeymapId == null) {
      log.warning(
          'Error while setting keyboard mapping: $value not found in supported keymaps');
    }

    if (_keymap != value) {
      _keymap = value;
      final KeyboardSettings newKeyboardSettings = KeyboardSettings(
        keymap: newKeymapId,
      );
      _proxy?.set(newKeyboardSettings).catchError((e) {
        log.warning('Error while setting keyboard mapping: $e');
      });
    }
  }

  List<String> get supportedKeymaps => _keymapIds.values.toList();

  @override
  Future<void> start() async {
    if (_proxy != null) {
      return;
    }

    _proxy = KeyboardProxy();
    Incoming.fromSvcPath().connectToService(_proxy);

    _keyboardSubscription =
        _proxy!.watch().asStream().listen(_onKeyboardSettingsChanged);
  }

  @override
  Future<void> stop() async {
    await _keyboardSubscription?.cancel();
    dispose();
  }

  @override
  void dispose() {
    _proxy?.ctrl.close();
    _proxy = null;
  }

  void _onKeyboardSettingsChanged(KeyboardSettings settings) {
    _keyboardSettings = settings;
    final newKeymap = _keymapIds[_keyboardSettings?.keymap] ?? 'usQwerty';
    if (_keymap != newKeymap) {
      _keymap = newKeymap;
    }
    onChanged();

    _keyboardSubscription =
        _proxy!.watch().asStream().listen(_onKeyboardSettingsChanged);
  }
}
