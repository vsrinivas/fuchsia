// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine/src/services/settings/battery_watcher_service.dart';
import 'package:ermine/src/services/settings/brightness_service.dart';
import 'package:ermine/src/services/settings/channel_service.dart';
import 'package:ermine/src/services/settings/data_sharing_consent_service.dart';
import 'package:ermine/src/services/settings/datetime_service.dart';
import 'package:ermine/src/services/settings/keyboard_service.dart';
import 'package:ermine/src/services/settings/memory_watcher_service.dart';
import 'package:ermine/src/services/settings/network_address_service.dart';
import 'package:ermine/src/services/settings/task_service.dart';
import 'package:ermine/src/services/settings/timezone_service.dart';
import 'package:ermine/src/services/settings/volume_service.dart';
import 'package:ermine/src/services/settings/wifi_service.dart';
import 'package:ermine/src/states/settings_state_impl.dart';
import 'package:ermine/src/widgets/quick_settings.dart';
import 'package:ermine/src/widgets/settings/setting_details.dart';
import 'package:ermine_utils/ermine_utils.dart';
import 'package:flutter/material.dart' hide Action;

enum WiFiStrength { off, searching, weak, good, strong, error }

enum BatteryCharge { missing, charging, discharging, error }

enum ChannelState {
  idle,
  checkingForUpdates,
  errorCheckingForUpdate,
  noUpdateAvailable,
  installationDeferredByPolicy,
  installingUpdate,
  waitingForReboot,
  installationError
}

/// Defines the pages that have a [SettingDetails] widget.
enum SettingsPage {
  none,
  wifi,
  bluetooth,
  channel,
  dataSharingConsent,
  timezone,
  shortcuts,
  feedback,
  opensource,
  brightness,
  about,
  keyboard
}

typedef DisplayDialogCallback = void Function(DialogInfo);

/// Defines the state of the [QuickSettings] overlay.
abstract class SettingsState implements TaskService {
  bool get allSettingsPageVisible;
  bool get shortcutsPageVisible;
  bool get timezonesPageVisible;
  bool get aboutPageVisible;
  bool get channelPageVisible;
  bool get dataSharingConsentPageVisible;
  bool get wifiPageVisible;
  bool get keyboardPageVisible;
  WiFiStrength get wifiStrength;
  BatteryCharge get batteryCharge;
  String get dateTime;
  String get selectedTimezone;
  List<String> get networkAddresses;
  String get memUsed;
  String get memTotal;
  double? get memPercentUsed;
  IconData get powerIcon;
  double? get powerLevel;
  Map<String, Set<String>> get shortcutBindings;
  List<String> get timezones;
  double? get brightnessLevel;
  bool? get brightnessAuto;
  IconData get brightnessIcon;
  bool? get optedIntoUpdates;
  String get currentChannel;
  List<String> get availableChannels;
  String get targetChannel;
  ChannelState get channelState;
  double get systemUpdateProgress;
  IconData get volumeIcon;
  double? get volumeLevel;
  bool? get volumeMuted;
  List<NetworkInformation> get availableNetworks;
  NetworkInformation get targetNetwork;
  List<NetworkInformation> get savedNetworks;
  TextEditingController get networkPasswordTextController;
  String get currentNetwork;
  bool get clientConnectionsEnabled;
  bool get clientConnectionsMonitor;
  int get wifiToggleMillisecondsPassed;
  bool get dataSharingConsentEnabled;
  String get currentKeymap;
  List<String> get supportedKeymaps;

  factory SettingsState.from(
      {required Map<String, Set<String>> shortcutBindings,
      required DisplayDialogCallback displayDialog}) {
    // ignore: unnecessary_cast
    return SettingsStateImpl(
      shortcutBindings: shortcutBindings,
      displayDialog: displayDialog,
      timezoneService: TimezoneService(),
      dataSharingConsentService: DataSharingConsentService(),
      dateTimeService: DateTimeService(),
      networkService: NetworkAddressService(),
      memoryWatcherService: MemoryWatcherService(),
      batteryWatcherService: BatteryWatcherService(),
      brightnessService: BrightnessService(),
      channelService: ChannelService(),
      volumeService: VolumeService(),
      wifiService: WiFiService(),
      keyboardService: KeyboardService(),
    ) as SettingsState;
  }

  void updateTimezone(String tz);
  void showAllSettings();
  void showShortcutSettings();
  void showTimezoneSettings();
  void setBrightnessLevel(double value);
  void setBrightnessAuto();
  void increaseBrightness();
  void decreaseBrightness();
  void showAboutSettings();
  void showChannelSettings();
  void showDataSharingConsentSettings();
  void setTargetChannel(String channel);
  void checkForUpdates();
  void setVolumeLevel(double value);
  void setVolumeMute({bool muted});
  void showWiFiSettings();
  void connectToNetwork([String password]);
  void setTargetNetwork(NetworkInformation network);
  void clearTargetNetwork();
  void removeNetwork(NetworkInformation network);
  void increaseVolume();
  void decreaseVolume();
  void toggleMute();
  void setClientConnectionsEnabled({bool enabled});
  void setDataSharingConsent({bool enabled});
  void showKeyboardSettings();
  void updateKeymap(String id);
}
