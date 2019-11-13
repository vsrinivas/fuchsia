// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:flutter/material.dart';

import 'package:fidl_fuchsia_power/fidl_async.dart';
import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:fuchsia_services/services.dart' show StartupContext;
import 'package:internationalization/strings.dart';
import 'package:quickui/quickui.dart';

/// Defines a [UiSpec] for visualizing battery.
class Battery extends UiSpec {
  // Localized strings.
  static String get _title => Strings.battery;

  BatteryModel model;

  Battery({BatteryManagerProxy monitor, BatteryInfoWatcherBinding binding}) {
    model = BatteryModel(
      monitor: monitor,
      binding: binding,
      onChange: _onChange,
    );
  }

  factory Battery.fromStartupContext(StartupContext startupContext) {
    final batteryManager = BatteryManagerProxy();
    startupContext.incoming.connectToService(batteryManager);
    return Battery(monitor: batteryManager);
  }

  void _onChange() {
    spec = _specForBattery(model.battery, model.charging);
  }

  @override
  void update(Value value) async {}

  @override
  void dispose() {
    model.dispose();
  }

  static Spec _specForBattery(double value, bool charging) {
    if (value.isNaN || value == 0) {
      return null;
    }
    final batteryText = '${value.toStringAsFixed(0)}%';
    if (value == 100) {
      return Spec(title: _title, groups: [
        Group(title: _title, values: [
          Value.withIcon(IconValue(codePoint: Icons.battery_full.codePoint)),
          Value.withText(TextValue(text: batteryText)),
        ]),
      ]);
    } else if (charging) {
      return Spec(title: _title, groups: [
        Group(title: _title, values: [
          Value.withIcon(
              IconValue(codePoint: Icons.battery_charging_full.codePoint)),
          Value.withText(TextValue(text: batteryText)),
        ]),
      ]);
    } else if (value <= 10) {
      return Spec(title: _title, groups: [
        Group(title: _title, values: [
          Value.withIcon(IconValue(codePoint: Icons.battery_alert.codePoint)),
          Value.withText(TextValue(text: batteryText)),
        ]),
      ]);
    } else {
      return Spec(title: _title, groups: [
        Group(title: _title, values: [
          Value.withText(TextValue(text: batteryText)),
        ]),
      ]);
    }
  }
}

class BatteryModel {
  final VoidCallback onChange;
  final BatteryInfoWatcherBinding _binding;
  final BatteryManagerProxy _monitor;

  double _battery;
  bool charging;

  BatteryModel({
    @required this.onChange,
    BatteryInfoWatcherBinding binding,
    BatteryManagerProxy monitor,
  })  : _binding = binding ?? BatteryInfoWatcherBinding(),
        _monitor = monitor {
    _monitor
      ..watch(_binding.wrap(_BatteryInfoWatcherImpl(this)))
      ..getBatteryInfo().then(_updateBattery);
  }

  void dispose() {
    _monitor.ctrl.close();
    _binding.close();
  }

  double get battery => _battery;
  set battery(double value) {
    _battery = value;
    onChange?.call();
  }

  void _updateBattery(BatteryInfo info) {
    if (info.levelPercent.isNaN) {
      _monitor.ctrl.close();
      _binding.close();
    } else {
      final chargeStatus = info.chargeStatus;
      charging = chargeStatus == ChargeStatus.charging;
      battery = info.levelPercent;
    }
  }
}

class _BatteryInfoWatcherImpl extends BatteryInfoWatcher {
  final BatteryModel batteryModel;
  _BatteryInfoWatcherImpl(this.batteryModel);

  @override
  Future<void> onChangeBatteryInfo(BatteryInfo info) async {
    batteryModel._updateBattery(info);
  }
}
