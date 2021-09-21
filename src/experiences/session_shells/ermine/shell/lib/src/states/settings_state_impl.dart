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
import 'package:ermine_utils/ermine_utils.dart';
import 'package:flutter/material.dart' hide Action;
import 'package:intl/intl.dart';
import 'package:mobx/mobx.dart';

/// Defines the implementation of [SettingsState].
class SettingsStateImpl with Disposable implements SettingsState, TaskService {
  static const kTimezonesFile = '/pkg/data/tz_ids.txt';

  final settingsPage = SettingsPage.none.asObservable();

  @override
  bool get shortcutsPageVisible => _shortcutsPageVisible.value;
  late final _shortcutsPageVisible =
      (() => settingsPage.value == SettingsPage.shortcuts).asComputed();

  @override
  bool get allSettingsPageVisible => _allSettingsPageVisible.value;
  late final _allSettingsPageVisible =
      (() => settingsPage.value == SettingsPage.none).asComputed();

  @override
  bool get timezonesPageVisible => _timezonesPageVisible.value;
  late final _timezonesPageVisible =
      (() => settingsPage.value == SettingsPage.timezone).asComputed();

  @override
  bool get aboutPageVisible => _aboutPageVisible.value;
  late final _aboutPageVisible =
      (() => settingsPage.value == SettingsPage.about).asComputed();

  @override
  bool get channelPageVisible => _channelPageVisible.value;
  late final _channelPageVisible =
      (() => settingsPage.value == SettingsPage.channel).asComputed();

  @override
  WiFiStrength get wifiStrength => _wifiStrength.value;
  final _wifiStrength = Observable<WiFiStrength>(WiFiStrength.good);

  @override
  BatteryCharge get batteryCharge => _batteryCharge.value;
  final _batteryCharge = Observable<BatteryCharge>(BatteryCharge.charging);

  @override
  final Map<String, Set<String>> shortcutBindings;

  @override
  String get selectedTimezone => _selectedTimezone.value;
  set selectedTimezone(String value) => _selectedTimezone.value = value;
  final Observable<String> _selectedTimezone;

  @override
  final networkAddresses = ObservableList<String>();

  @override
  String get memUsed => _memUsed.value;
  set memUsed(String value) => _memUsed.value = value;
  final Observable<String> _memUsed = '--'.asObservable();

  @override
  String get memTotal => _memTotal.value;
  set memTotal(String value) => _memTotal.value = value;
  final Observable<String> _memTotal = '--'.asObservable();

  @override
  double? get memPercentUsed => _memPercentUsed.value;
  set memPercentUsed(double? value) => _memPercentUsed.value = value;
  final Observable<double?> _memPercentUsed = Observable<double?>(null);

  @override
  IconData get powerIcon => _powerIcon.value;
  set powerIcon(IconData value) => _powerIcon.value = value;
  final Observable<IconData> _powerIcon = Icons.battery_unknown.asObservable();

  @override
  double? get powerLevel => _powerLevel.value;
  set powerLevel(double? value) => _powerLevel.value = value;
  final Observable<double?> _powerLevel = Observable<double?>(null);

  @override
  double? get brightnessLevel => _brightnessLevel.value;
  set brightnessLevel(double? value) => _brightnessLevel.value = value;
  final Observable<double?> _brightnessLevel = Observable<double?>(null);

  @override
  bool? get brightnessAuto => _brightnessAuto.value;
  set brightnessAuto(bool? value) => _brightnessAuto.value = value;
  final Observable<bool?> _brightnessAuto = Observable<bool?>(null);

  @override
  IconData get brightnessIcon => _brightnessIcon.value;
  set brightnessIcon(IconData value) => _brightnessIcon.value = value;
  final Observable<IconData> _brightnessIcon =
      Icons.brightness_auto.asObservable();

  @override
  bool? get optedIntoUpdates => _optedIntoUpdates.value;
  set optedIntoUpdates(bool? value) => _optedIntoUpdates.value = value;
  final Observable<bool?> _optedIntoUpdates = Observable<bool?>(null);

  @override
  String get currentChannel => _currentChannel.value;
  set currentChannel(String value) => _currentChannel.value = value;
  final Observable<String> _currentChannel = Observable<String>('');

  @override
  final List<String> availableChannels = ObservableList<String>();

  @override
  String get targetChannel => _targetChannel.value;
  set targetChannel(String value) => _targetChannel.value = value;
  final Observable<String> _targetChannel = Observable<String>('');

  @override
  ChannelState get channelState => _channelState.value;
  set channelState(ChannelState value) => _channelState.value = value;
  final _channelState = Observable<ChannelState>(ChannelState.idle);

  final List<String> _timezones;

  @override
  List<String> get timezones {
    // Move the selected timezone to the top.
    return [selectedTimezone]
      ..addAll(_timezones.where((zone) => zone != selectedTimezone));
  }

  @override
  String get dateTime => _dateTime.value;
  late final ObservableValue<String> _dateTime = (() =>
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
        _selectedTimezone = timezoneService.timezone.asObservable() {
    dateTimeService.onChanged = updateDateTime;
    timezoneService.onChanged =
        (timezone) => runInAction(() => selectedTimezone = timezone);
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
        memUsed = '${memoryWatcherService.memUsed!.toStringAsPrecision(2)}GB';
        memTotal = '${memoryWatcherService.memTotal!.toStringAsPrecision(2)}GB';
        memPercentUsed =
            memoryWatcherService.memUsed! / memoryWatcherService.memTotal!;
      });
    };
    batteryWatcherService.onChanged = () {
      runInAction(() {
        powerIcon = batteryWatcherService.icon;
        powerLevel = batteryWatcherService.levelPercent;
      });
    };
    brightnessService.onChanged = () {
      runInAction(() {
        brightnessLevel = brightnessService.brightness;
        brightnessAuto = brightnessService.auto;
        brightnessIcon = brightnessService.icon;
      });
    };
    channelService.onChanged = () {
      runInAction(() {
        optedIntoUpdates = channelService.optedIntoUpdates;
        currentChannel = channelService.currentChannel;
        // Ensure current channel is listed first in available channels
        List<String> channels;
        if (channelService.channels.contains(currentChannel)) {
          channels = channelService.channels;
          int index = channels.indexOf(currentChannel);
          if (index != 0) {
            channels
              ..removeAt(index)
              ..insert(0, currentChannel);
          }
        } else {
          channels = [currentChannel]..addAll(availableChannels);
        }
        availableChannels
          ..clear()
          ..addAll(channels);
        targetChannel = channelService.targetChannel;
        // Monitor state of update
        if (channelService.checkingForUpdates) {
          channelState = ChannelState.checkingForUpdates;
        } else if (channelService.errorCheckingForUpdate) {
          channelState = ChannelState.errorCheckingForUpdate;
        } else if (channelService.noUpdateAvailable) {
          channelState = ChannelState.noUpdateAvailable;
        } else if (channelService.installationDeferredByPolicy) {
          channelState = ChannelState.installationDeferredByPolicy;
        } else if (channelService.installingUpdate) {
          channelState = ChannelState.installingUpdate;
        } else if (channelService.waitingForReboot) {
          channelState = ChannelState.waitingForReboot;
        } else if (channelService.installationError) {
          channelState = ChannelState.installationError;
        }
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
  void updateTimezone(String timezone) => runInAction(() {
        selectedTimezone = timezone;
        timezoneService.timezone = timezone;
        settingsPage.value = SettingsPage.none;
      });

  @override
  void showAllSettings() =>
      runInAction(() => settingsPage.value = SettingsPage.none);

  @override
  void showAboutSettings() =>
      runInAction(() => settingsPage.value = SettingsPage.about);

  @override
  void showShortcutSettings() =>
      runInAction(() => settingsPage.value = SettingsPage.shortcuts);

  @override
  void showTimezoneSettings() =>
      runInAction(() => settingsPage.value = SettingsPage.timezone);

  @override
  void showChannelSettings() =>
      runInAction(() => settingsPage.value = SettingsPage.channel);

  @override
  void setTargetChannel(String value) =>
      runInAction(() => channelService.targetChannel = value);

  @override
  void setBrightnessLevel(double value) =>
      runInAction(() => brightnessService.brightness = value);

  @override
  void setBrightnessAuto() => runInAction(() => brightnessService.auto = true);

  Observable<DateTime>? _dateTimeNow;
  Observable<DateTime> get dateTimeNow =>
      _dateTimeNow ??= DateTime.now().asObservable();

  late final Action updateDateTime = () {
    dateTimeNow.value = DateTime.now();
  }.asAction();

  @override
  void checkForUpdates() => runInAction(channelService.checkForUpdates);

  static List<String> _loadTimezones() {
    return File(kTimezonesFile).readAsLinesSync();
  }
}
