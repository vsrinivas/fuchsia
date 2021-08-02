// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:ermine/src/services/settings/battery_watcher_service.dart';
import 'package:ermine/src/services/settings/brightness_service.dart';
import 'package:ermine/src/services/settings/channel_service.dart';
import 'package:ermine/src/services/settings/datetime_service.dart';
import 'package:ermine/src/services/settings/memory_watcher_service.dart';
import 'package:ermine/src/services/settings/network_address_service.dart';
import 'package:ermine/src/services/settings/task_service.dart';
import 'package:ermine/src/services/settings/timezone_service.dart';
import 'package:ermine/src/services/shortcuts_service.dart';
import 'package:ermine/src/states/settings_state.dart';
import 'package:ermine/src/utils/mobx_disposable.dart';
import 'package:ermine/src/utils/mobx_extensions.dart';
import 'package:flutter/material.dart' hide Action;
import 'package:intl/intl.dart';
import 'package:mobx/mobx.dart';

/// Defines the implementation of [SettingsState].
class SettingsStateImpl with Disposable implements SettingsState, TaskService {
  static const kTimezonesFile = '/pkg/data/tz_ids.txt';

  final settingsPage = SettingsPage.none.asObservable();

  @override
  late final shortcutsPageVisible =
      (() => settingsPage.value == SettingsPage.shortcuts).asComputed();

  @override
  late final allSettingsPageVisible =
      (() => settingsPage.value == SettingsPage.none).asComputed();

  @override
  late final timezonesPageVisible =
      (() => settingsPage.value == SettingsPage.timezone).asComputed();

  @override
  late final aboutPageVisible =
      (() => settingsPage.value == SettingsPage.about).asComputed();

  @override
  final wifiStrength = Observable<WiFiStrength>(WiFiStrength.good);

  @override
  final batteryCharge = Observable<BatteryCharge>(BatteryCharge.charging);

  @override
  final Map<String, Set<String>> shortcutBindings;

  @override
  final Observable<String> selectedTimezone;

  @override
  final networkAddresses = ObservableList<String>();

  @override
  final Observable<String> memUsed = '--'.asObservable();

  @override
  final Observable<String> memTotal = '--'.asObservable();

  @override
  final Observable<double?> memPercentUsed = Observable<double?>(null);

  @override
  final Observable<IconData> powerIcon = Icons.battery_unknown.asObservable();

  @override
  final Observable<double?> powerLevel = Observable<double?>(null);

  @override
  final Observable<double?> brightnessLevel = Observable<double?>(null);

  @override
  final Observable<bool?> brightnessAuto = Observable<bool?>(null);

  @override
  final Observable<IconData> brightnessIcon =
      Icons.brightness_auto.asObservable();

  @override
  final Observable<bool?> optedIntoUpdates = Observable<bool?>(null);

  @override
  final Observable<String> currentChannel = Observable<String>('');

  final List<String> _timezones;

  @override
  List<String> get timezones {
    // Move the selected timezone to the top.
    return [selectedTimezone.value]
      ..addAll(_timezones.where((zone) => zone != selectedTimezone.value));
  }

  @override
  late final ObservableValue<String> dateTime = (() =>
      // Ex: Mon, Jun 7 2:25 AM
      DateFormat.MMMEd().add_jm().format(dateTimeNow.value)).asComputed();

  final DateTimeService dateTimeService;
  final TimezoneService timezoneService;
  final NetworkAddressService networkService;
  final MemoryWatcherService memoryWatcherService;
  final BatteryWatcherService batteryWatcherService;
  final BrightnessService brightnessService;
  final ChannelService channelService;

  SettingsStateImpl({
    required ShortcutsService shortcutsService,
    required this.timezoneService,
    required this.dateTimeService,
    required this.networkService,
    required this.memoryWatcherService,
    required this.batteryWatcherService,
    required this.brightnessService,
    required this.channelService,
  })  : shortcutBindings = shortcutsService.keyboardBindings,
        _timezones = _loadTimezones(),
        selectedTimezone = timezoneService.timezone.asObservable() {
    dateTimeService.onChanged = updateDateTime;
    timezoneService.onChanged =
        (timezone) => runInAction(() => selectedTimezone.value = timezone);
    networkService.onChanged = () => NetworkInterface.list().then((interfaces) {
          // Gather all addresses from all interfaces and sort them such that
          // IPv4 addresses come before IPv6.
          final addresses = interfaces
              .expand((interface) => interface.addresses)
              .toList(growable: false)
            ..sort((addr1, addr2) =>
                addr1.type == InternetAddressType.IPv4 ? -1 : 0);

          runInAction(() => networkAddresses
            ..clear()
            ..addAll(addresses.map((address) => address.address)));
        });
    memoryWatcherService.onChanged = () {
      runInAction(() {
        memUsed.value =
            '${memoryWatcherService.memUsed!.toStringAsPrecision(2)}GB';
        memTotal.value =
            '${memoryWatcherService.memTotal!.toStringAsPrecision(2)}GB';
        memPercentUsed.value =
            memoryWatcherService.memUsed! / memoryWatcherService.memTotal!;
      });
    };
    batteryWatcherService.onChanged = () {
      runInAction(() {
        powerIcon.value = batteryWatcherService.icon;
        powerLevel.value = batteryWatcherService.levelPercent;
      });
    };
    brightnessService.onChanged = () {
      runInAction(() {
        brightnessLevel.value = brightnessService.brightness;
        brightnessAuto.value = brightnessService.auto;
        brightnessIcon.value = brightnessService.icon;
      });
    };
    channelService.onChanged = () {
      runInAction(() {
        optedIntoUpdates.value = channelService.optedIntoUpdates;
        currentChannel.value = channelService.currentChannel;
      });
    };
  }

  @override
  Future<void> start() async {
    await Future.wait([
      dateTimeService.start(),
      timezoneService.start(),
      networkService.start(),
      memoryWatcherService.start(),
      batteryWatcherService.start(),
      brightnessService.start(),
      channelService.start(),
    ]);
  }

  @override
  Future<void> stop() async {
    showAllSettings();
    await dateTimeService.stop();
    await timezoneService.stop();
    await networkService.stop();
    await memoryWatcherService.stop();
    await batteryWatcherService.stop();
    await brightnessService.stop();
    await channelService.stop();
    _dateTimeNow = null;
  }

  @override
  void dispose() {
    super.dispose();
    dateTimeService.dispose();
    timezoneService.dispose();
    networkService.dispose();
    memoryWatcherService.dispose();
    batteryWatcherService.dispose();
    channelService.dispose();
  }

  @override
  late final Action updateTimezone = (timezone) {
    selectedTimezone.value = timezone;
    timezoneService.timezone = timezone;
    settingsPage.value = SettingsPage.none;
  }.asAction();

  @override
  late final Action showAllSettings = () {
    settingsPage.value = SettingsPage.none;
  }.asAction();

  @override
  late final Action showAboutSettings = () {
    settingsPage.value = SettingsPage.about;
  }.asAction();

  @override
  late final Action showShortcutSettings = () {
    settingsPage.value = SettingsPage.shortcuts;
  }.asAction();

  @override
  late final Action showTimezoneSettings = () {
    settingsPage.value = SettingsPage.timezone;
  }.asAction();

  @override
  late final Action setBrightnessLevel = (double value) {
    brightnessService.brightness = value;
  }.asAction();

  @override
  late final Action setBrightnessAuto = (bool value) {
    brightnessService.auto = value;
  }.asAction();

  Observable<DateTime>? _dateTimeNow;
  Observable<DateTime> get dateTimeNow =>
      _dateTimeNow ??= DateTime.now().asObservable();

  late final Action updateDateTime = () {
    dateTimeNow.value = DateTime.now();
  }.asAction();

  static List<String> _loadTimezones() {
    return File(kTimezonesFile).readAsLinesSync();
  }
}
