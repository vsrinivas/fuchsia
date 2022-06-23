// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:ermine/src/services/settings/task_service.dart';
import 'package:fidl_fuchsia_intl/fidl_async.dart';
import 'package:fidl_fuchsia_settings/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:fuchsia_services/services.dart';

/// Defines a [TaskService] to control keyboard mappings.
class KeyboardService implements TaskService {
  late final VoidCallback onChanged;

  KeyboardProxy? _proxy;
  KeyboardSettings? _keyboardSettings;
  StreamSubscription? _keyboardSubscription;

  KeyboardService();

  // TODO(fxb/79589): Convert keymap to readable string via keymap.fidl
  String get currentKeyMap => _keyboardSettings?.keymap.toString() ?? '';

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
    onChanged();

    _keyboardSubscription =
        _proxy!.watch().asStream().listen(_onKeyboardSettingsChanged);
  }
}
