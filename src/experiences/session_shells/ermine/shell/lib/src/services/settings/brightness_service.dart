// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:ermine/src/services/settings/task_service.dart';
import 'package:fidl_fuchsia_ui_brightness/fidl_async.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:fuchsia_services/services.dart';

/// Defines a [TaskService] for updating and responding to brightness.
class BrightnessService implements TaskService {
  late final VoidCallback onChanged;

  ControlProxy? _control;
  StreamSubscription? _brightnessSubscription;
  StreamSubscription? _autoSubscription;
  late bool _auto;
  late double _brightness;

  BrightnessService();

  double get brightness => _brightness;
  set brightness(double value) {
    // Setting new brightness should only work when connected (started).
    assert(_control != null, 'BrightnessService not started');

    if (_control != null && _brightness != value) {
      _brightness = value;
      _auto = false;
      _control!.setManualBrightness(value);
    }
  }

  IconData get icon => _auto ? Icons.brightness_auto : Icons.brightness_5;

  bool get auto => _auto;
  set auto(bool value) {
    _auto = value;
    if (auto) {
      _control!.setAutoBrightness();
    }
    onChanged();
  }

  @override
  Future<void> start() async {
    _control = ControlProxy();
    Incoming.fromSvcPath().connectToService(_control);

    // Determine if auto brightness enabled
    _autoSubscription =
        _control!.watchAutoBrightness().asStream().listen((auto) {
      _auto = auto;
      if (_auto) {
        _control!.setAutoBrightness();
      }
    });

    // Watch for changes in brightness value
    _control!
        .watchCurrentBrightness()
        .asStream()
        .listen(_onBrightnessSettingsChanged);
  }

  @override
  Future<void> stop() async {
    await _brightnessSubscription?.cancel();
    await _autoSubscription?.cancel();
    dispose();
  }

  @override
  void dispose() {
    _control?.ctrl.close();
    _control = null;
  }

  void _onBrightnessSettingsChanged(double value) {
    _brightness = value;
    onChanged();
    _brightnessSubscription = _control!
        .watchCurrentBrightness()
        .asStream()
        .listen(_onBrightnessSettingsChanged);
  }
}
