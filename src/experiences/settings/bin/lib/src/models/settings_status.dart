// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl_fuchsia_timezone/fidl_async.dart' as tz;
import 'package:fidl_fuchsia_wlan_service/fidl_async.dart' as wlan;
import 'package:flutter/foundation.dart';
import 'package:fuchsia_services/services.dart';

// Interval for scanning in seconds.
const _statusUpdateInterval = 6;

/// Class that listens to and sends out status shown in the all_settings view.
class SettingsStatus extends ChangeNotifier {
  final ValueNotifier<String> _wifiStatus = ValueNotifier<String>('');
  final ValueNotifier<String> _timezoneStatus = ValueNotifier<String>('');

  final wlan.WlanProxy _wlanProxy = wlan.WlanProxy();
  final tz.TimezoneProxy _timezoneProxy = tz.TimezoneProxy();
  final tz.TimezoneWatcherBinding _timezoneWatcherBinding =
      tz.TimezoneWatcherBinding();

  Timer _wlanUpdateTimer;
  SettingsStatus() {
    _wifiStatus.addListener(notifyListeners);
    _timezoneStatus.addListener(notifyListeners);

    _listenToWifiStatus();
    _listenToTimezoneStatus();
  }

  String get timezoneStatus => _timezoneStatus.value;

  String get wifiStatus => _wifiStatus.value;

  /// Stop any updating timers
  void stop() {
    _wlanUpdateTimer.cancel();
  }

  String _extractTimezoneStatus(String timezoneId) => '$timezoneId';

  String _extractWifiStatus(wlan.WlanStatus status) {
    switch (status.state) {
      case wlan.State.associated:
        return 'Connected to ${status.currentAp.ssid}';
      case wlan.State.associating:
      case wlan.State.joining:
      case wlan.State.bss:
      case wlan.State.querying:
      case wlan.State.authenticating:
        return 'Connecting';
      case wlan.State.scanning:
        return 'Disconnected';
      default:
        return 'Unknown';
    }
  }

  void _listenToTimezoneStatus() async {
    StartupContext.fromStartupInfo().incoming.connectToService(_timezoneProxy);

    _timezoneStatus.value =
        _extractTimezoneStatus(await _timezoneProxy.getTimezoneId());

    await _timezoneProxy
        .watch(_timezoneWatcherBinding.wrap(_TimezoneWatcherImpl(this)));
  }

  void _listenToWifiStatus() async {
    StartupContext.fromStartupInfo().incoming.connectToService(_wlanProxy);

    _wifiStatus.value = _extractWifiStatus(await _wlanProxy.status());

    _wlanUpdateTimer =
        Timer.periodic(Duration(seconds: _statusUpdateInterval), (_) async {
      _wifiStatus.value = _extractWifiStatus(await _wlanProxy.status());
    });
  }

  void _onChangeTimezone(String tz) {
    _timezoneStatus.value = _extractTimezoneStatus(tz);
  }
}

class _TimezoneWatcherImpl extends tz.TimezoneWatcher {
  final SettingsStatus status;
  _TimezoneWatcherImpl(this.status);

  @override
  Future<void> onTimezoneOffsetChange(String timezoneId) async {
    status._onChangeTimezone(timezoneId);
  }
}
