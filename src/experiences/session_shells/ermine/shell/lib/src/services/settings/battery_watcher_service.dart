// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine/src/services/settings/task_service.dart';
import 'package:fidl_fuchsia_power_battery/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:fuchsia_services/services.dart';

/// Defines a [TaskService] to watch for battery status in the system.
class BatteryWatcherService extends BatteryInfoWatcher implements TaskService {
  late final VoidCallback onChanged;

  BatteryManagerProxy? _proxy;
  BatteryInfoWatcherBinding? _binding;

  @override
  Future<void> start() async {
    _proxy = BatteryManagerProxy();
    Incoming.fromSvcPath().connectToService(_proxy);
    _binding = BatteryInfoWatcherBinding();
    try {
      await _proxy!.watch(_binding!.wrap(this));
      await _proxy!.getBatteryInfo().then(onChangeBatteryInfo);
      // ignore: avoid_catches_without_on_clauses
    } catch (_) {
      _proxy = null;
      _binding = null;
    }
  }

  @override
  Future<void> stop() async {}

  @override
  void dispose() {
    _binding?.close();
    _proxy?.ctrl.close();
  }

  BatteryInfo? _info;
  bool _isLevelDropping = false;
  bool get hasStatus => _info?.status != BatteryStatus.unknown;
  bool get hasBattery => _info?.status == BatteryStatus.ok;
  bool get isCharging => _info?.chargeStatus == ChargeStatus.charging;
  bool get isFull => _info?.chargeStatus == ChargeStatus.full;
  bool get hasAlert =>
      _info?.levelStatus == LevelStatus.critical ||
      _info?.levelStatus == LevelStatus.low ||
      _info?.levelStatus == LevelStatus.warning;

  double? get levelPercent => _info?.levelPercent;
  bool get isLevelDropping => _isLevelDropping;

  IconData get icon => hasStatus
      ? hasBattery
          ? hasAlert
              ? Icons.battery_alert
              : isCharging
                  ? Icons.battery_charging_full
                  : isFull
                      ? Icons.battery_full
                      : Icons.battery_std
          : Icons.electrical_services
      : Icons.battery_unknown;

  @override
  Future<void> onChangeBatteryInfo(BatteryInfo info) async {
    final newLevel = info.levelPercent?.toInt();
    final oldLevel = _info?.levelPercent?.toInt();
    _isLevelDropping =
        newLevel != null && oldLevel != null && newLevel < oldLevel;
    _info = info;
    onChanged();
  }
}
