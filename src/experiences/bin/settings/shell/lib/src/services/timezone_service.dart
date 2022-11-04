// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl_fuchsia_intl/fidl_async.dart';
import 'package:fidl_fuchsia_settings/fidl_async.dart';
import 'package:flutter/foundation.dart';
import 'package:fuchsia_services/services.dart';
import 'package:shell_settings/src/services/task_service.dart';

/// Defines a [TaskService] for updating and responding to timezones.
class TimezoneService implements TaskService {
  late final ValueChanged<String> onChanged;

  IntlProxy? _settingsService;
  IntlSettings? _intlSettings;
  String _timezone = 'America/Los_Angeles';
  StreamSubscription<IntlSettings>? _subscription;

  TimezoneService();

  String get timezone => _timezone;
  set timezone(String value) {
    // Setting new timezone should only work when connected (started).
    assert(_settingsService != null, 'TimezoneService not started');

    if (_settingsService != null && _timezone != value) {
      _timezone = value;
      final IntlSettings newIntlSettings = IntlSettings(
          locales: _intlSettings?.locales,
          temperatureUnit: _intlSettings?.temperatureUnit,
          timeZoneId: TimeZoneId(id: value));
      _settingsService!.set(newIntlSettings);
    }
  }

  @override
  Future<void> start() async {
    _settingsService = IntlProxy();
    Incoming.fromSvcPath().connectToService(_settingsService);

    _subscription =
        _settingsService!.watch().asStream().listen(_onIntlSettingsChanged);
  }

  @override
  Future<void> stop() async {
    await _subscription?.cancel();
    dispose();
  }

  @override
  void dispose() {
    _settingsService?.ctrl.close();
    _settingsService = null;
  }

  void _onIntlSettingsChanged(IntlSettings intlSettings) {
    _intlSettings = intlSettings;
    if (_timezone != intlSettings.timeZoneId?.id) {
      _timezone = intlSettings.timeZoneId!.id;
      onChanged(_timezone);
    }
    // Go back to watching for next update.
    _subscription =
        _settingsService!.watch().asStream().listen(_onIntlSettingsChanged);
  }
}
