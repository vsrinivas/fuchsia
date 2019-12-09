// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:fidl_fuchsia_power/fidl_async.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:settings/settings.dart';
import 'package:mockito/mockito.dart';

void main() {
  test('Battery', () async {
    final monitorProxy = MockMonitorProxy();
    final binding = MockBinding();

    Battery battery = Battery(monitor: monitorProxy, binding: binding);

    final BatteryInfoWatcher watcher =
        verify(binding.wrap(captureAny)).captured.single;
    await watcher.onChangeBatteryInfo(
        _buildStats(5, BatteryStatus.ok, ChargeStatus.charging));

    final spec = await battery.getSpec();

    TextValue text = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.text)
        .first
        ?.text;
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);
    expect(text?.text, '5%');
  });

  test('Change Battery Level', () async {
    final monitorProxy = MockMonitorProxy();
    final binding = MockBinding();

    Battery battery = Battery(monitor: monitorProxy, binding: binding);

    final BatteryInfoWatcher watcher =
        verify(binding.wrap(captureAny)).captured.single;
    await watcher.onChangeBatteryInfo(
        _buildStats(5, BatteryStatus.ok, ChargeStatus.charging));

    final spec = await battery.getSpec();

    TextValue text = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.text)
        .first
        ?.text;
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);
    expect(text?.text, '5%');

    // Change battery level
    await watcher.onChangeBatteryInfo(
        _buildStats(6, BatteryStatus.ok, ChargeStatus.notCharging));

    final updatedSpec = await battery.getSpec();

    text = updatedSpec.groups.first.values
        .where((v) => v.$tag == ValueTag.text)
        .first
        ?.text;
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);
    expect(text?.text, '6%');
  });

  test('Change Charging Status', () async {
    final monitorProxy = MockMonitorProxy();
    final binding = MockBinding();

    Battery battery = Battery(monitor: monitorProxy, binding: binding);

    final BatteryInfoWatcher watcher =
        verify(binding.wrap(captureAny)).captured.single;
    await watcher.onChangeBatteryInfo(
        _buildStats(50, BatteryStatus.ok, ChargeStatus.notCharging));

    Spec spec = await battery.getSpec();

    TextValue text = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.text)
        .first
        ?.text;
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);
    expect(text?.text, '50%');

    bool hasIcon = spec.groups.first.values.any((v) => v.$tag == ValueTag.icon);
    expect(hasIcon, isFalse);

    // Change charging status
    await watcher.onChangeBatteryInfo(
        _buildStats(51, BatteryStatus.ok, ChargeStatus.charging));

    spec = await battery.getSpec();
    text = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.text)
        .first
        ?.text;
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);
    expect(text?.text, '51%');

    hasIcon = spec.groups.first.values.any((v) => v.$tag == ValueTag.icon);
    expect(hasIcon, isTrue);
  });

  test('Low Battery Warning', () async {
    final monitorProxy = MockMonitorProxy();
    final binding = MockBinding();

    Battery battery = Battery(monitor: monitorProxy, binding: binding);

    final BatteryInfoWatcher watcher =
        verify(binding.wrap(captureAny)).captured.single;
    await watcher.onChangeBatteryInfo(
        _buildStats(50, BatteryStatus.ok, ChargeStatus.notCharging));

    Spec spec = await battery.getSpec();

    TextValue text = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.text)
        .first
        ?.text;
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);
    expect(text?.text, '50%');

    bool hasIcon = spec.groups.first.values.any((v) => v.$tag == ValueTag.icon);
    expect(hasIcon, isFalse);

    // Change charge to <10% (low status)
    await watcher.onChangeBatteryInfo(
        _buildStats(9, BatteryStatus.ok, ChargeStatus.notCharging));

    spec = await battery.getSpec();
    text = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.text)
        .first
        ?.text;
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);
    expect(text?.text, '9%');

    hasIcon = spec.groups.first.values.any((v) => v.$tag == ValueTag.icon);
    expect(hasIcon, isTrue);
  });

  test('Battery Full', () async {
    final monitorProxy = MockMonitorProxy();
    final binding = MockBinding();

    Battery battery = Battery(monitor: monitorProxy, binding: binding);

    final BatteryInfoWatcher watcher =
        verify(binding.wrap(captureAny)).captured.single;
    await watcher.onChangeBatteryInfo(
        _buildStats(50, BatteryStatus.ok, ChargeStatus.notCharging));

    Spec spec = await battery.getSpec();

    TextValue text = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.text)
        .first
        ?.text;
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);
    expect(text?.text, '50%');

    bool hasIcon = spec.groups.first.values.any((v) => v.$tag == ValueTag.icon);
    expect(hasIcon, isFalse);

    // Change charge to 100% (full)
    await watcher.onChangeBatteryInfo(
        _buildStats(100, BatteryStatus.ok, ChargeStatus.notCharging));

    spec = await battery.getSpec();
    text = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.text)
        .first
        ?.text;
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);
    expect(text?.text, '100%');

    hasIcon = spec.groups.first.values.any((v) => v.$tag == ValueTag.icon);
    expect(hasIcon, isTrue);
  });
}

BatteryInfo _buildStats(
    double power, BatteryStatus status, ChargeStatus charge) {
  // ignore: missing_required_param
  return BatteryInfo(
    levelPercent: power,
    status: status,
    chargeStatus: charge,
  );
}

// Mock classes.
class MockMonitorProxy extends Mock implements BatteryManagerProxy {}

class MockBinding extends Mock implements BatteryInfoWatcherBinding {}
