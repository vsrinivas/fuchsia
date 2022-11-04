// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(http://fxbug.dev/107480): Resolve lint issues and reenable analysis for file
// ignore_for_file: cascade_invocations

import 'dart:io';

import 'package:ermine_utils/ermine_utils.dart';
import 'package:flutter/material.dart' hide Action;
import 'package:internationalization/strings.dart';
import 'package:intl/intl.dart';
import 'package:mobx/mobx.dart';
import 'package:shell_settings/src/services/task_service.dart';
import 'package:shell_settings/src/services/timezone_service.dart';
import 'package:shell_settings/src/states/settings_state.dart';

/// Defines the implementation of [SettingsState].
class SettingsStateImpl with Disposable implements SettingsState, TaskService {
  static const kTimezonesFile = '/pkg/data/tz_ids.txt';

  final settingsPage = SettingsPage.none.asObservable();

  @override
  bool get allSettingsPageVisible => _allSettingsPageVisible.value;
  late final _allSettingsPageVisible =
      (() => settingsPage.value == SettingsPage.none).asComputed();

  @override
  bool get timezonesPageVisible => _timezonesPageVisible.value;
  late final _timezonesPageVisible =
      (() => settingsPage.value == SettingsPage.timezone).asComputed();

  @override
  String get selectedTimezone => _selectedTimezone.value;
  set selectedTimezone(String value) => _selectedTimezone.value = value;
  final Observable<String> _selectedTimezone;

  final List<String> _timezones;

  @override
  List<String> get timezones {
    // Move the selected timezone to the top.
    return [selectedTimezone]
      ..addAll(_timezones.where((zone) => zone != selectedTimezone));
  }

  final TimezoneService timezoneService;

  SettingsStateImpl({
    required this.timezoneService,
  })  : _timezones = _loadTimezones(),
        _selectedTimezone = timezoneService.timezone.asObservable() {
    timezoneService.onChanged =
        (timezone) => runInAction(() => selectedTimezone = timezone);
  }

  @override
  Future<void> start() async {
    await Future.wait([
      timezoneService.start(),
    ]);
  }

  @override
  Future<void> stop() async {
    showAllSettings();
    await timezoneService.stop();
  }

  @override
  void dispose() {
    super.dispose();
    timezoneService.dispose();
  }

  @override
  void updateTimezone(String timezone) => runInAction(() {
        selectedTimezone = timezone;
        timezoneService.timezone = timezone;
        settingsPage.value = SettingsPage.none;
      });

  @override
  void showAllSettings() =>
      runInAction(() => settingsPage.value = SettingsPage.none);

  @override
  void showTimezoneSettings() =>
      runInAction(() => settingsPage.value = SettingsPage.timezone);

  static List<String> _loadTimezones() {
    return File(kTimezonesFile).readAsLinesSync();
  }
}
