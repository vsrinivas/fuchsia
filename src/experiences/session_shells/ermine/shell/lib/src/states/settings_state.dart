// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine/src/services/settings/battery_watcher_service.dart';
import 'package:ermine/src/services/settings/brightness_service.dart';
import 'package:ermine/src/services/settings/channel_service.dart';
import 'package:ermine/src/services/settings/datetime_service.dart';
import 'package:ermine/src/services/settings/memory_watcher_service.dart';
import 'package:ermine/src/services/settings/network_address_service.dart';
import 'package:ermine/src/services/settings/task_service.dart';
import 'package:ermine/src/services/settings/timezone_service.dart';
import 'package:ermine/src/services/shortcuts_service.dart';
import 'package:ermine/src/states/settings_state_impl.dart';
import 'package:ermine/src/widgets/quick_settings.dart';
import 'package:ermine/src/widgets/settings/setting_details.dart';
import 'package:flutter/material.dart' hide Action;
import 'package:mobx/mobx.dart';

enum WiFiStrength { off, searching, weak, good, strong, error }

enum BatteryCharge { missing, charging, discharging, error }

/// Defines the pages that have a [SettingDetails] widget.
enum SettingsPage {
  none,
  wifi,
  bluetooth,
  channel,
  timezone,
  shortcuts,
  feedback,
  opensource,
  brightness,
  about
}

/// Defines the state of the [QuickSettings] overlay.
abstract class SettingsState with Store implements TaskService {
  ObservableValue<bool> get allSettingsPageVisible;
  ObservableValue<bool> get shortcutsPageVisible;
  ObservableValue<bool> get timezonesPageVisible;
  ObservableValue<bool> get aboutPageVisible;
  ObservableValue<bool> get channelPageVisible;
  ObservableValue<WiFiStrength> get wifiStrength;
  ObservableValue<BatteryCharge> get batteryCharge;
  ObservableValue<String> get dateTime;
  ObservableValue<String> get selectedTimezone;
  ObservableList<String> get networkAddresses;
  ObservableValue<String> get memUsed;
  ObservableValue<String> get memTotal;
  ObservableValue<double?> get memPercentUsed;
  ObservableValue<IconData> get powerIcon;
  ObservableValue<double?> get powerLevel;
  Map<String, Set<String>> get shortcutBindings;
  List<String> get timezones;
  ObservableValue<double?> get brightnessLevel;
  ObservableValue<bool?> get brightnessAuto;
  ObservableValue<IconData> get brightnessIcon;
  ObservableValue<bool?> get optedIntoUpdates;
  ObservableValue<String> get currentChannel;
  ObservableValue<List<String>> get availableChannels;

  factory SettingsState.from({required ShortcutsService shortcutsService}) {
    return SettingsStateImpl(
      shortcutsService: shortcutsService,
      timezoneService: TimezoneService(),
      dateTimeService: DateTimeService(),
      networkService: NetworkAddressService(),
      memoryWatcherService: MemoryWatcherService(),
      batteryWatcherService: BatteryWatcherService(),
      brightnessService: BrightnessService(),
      channelService: ChannelService(),
    );
  }

  Action get updateTimezone;
  Action get showAllSettings;
  Action get showShortcutSettings;
  Action get showTimezoneSettings;
  Action get setBrightnessLevel;
  Action get setBrightnessAuto;
  Action get showAboutSettings;
  Action get showChannelSettings;
  Action get setTargetChannel;
}
