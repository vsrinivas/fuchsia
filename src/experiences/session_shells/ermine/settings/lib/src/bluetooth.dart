// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:flutter/material.dart';

import 'package:fidl_fuchsia_bluetooth_control/fidl_async.dart' as bt;
import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:fuchsia_services/services.dart' show StartupContext;
import 'package:internationalization/strings.dart';
import 'package:quickui/quickui.dart';

/// Defines a [UiSpec] for visualizing bluetooth.
class Bluetooth extends UiSpec {
  // Localized strings.
  static String get _title => Strings.bluetooth;

  BluetoothModel model;

  Bluetooth({bt.ControlProxy monitor}) {
    model = BluetoothModel(
      monitor: monitor,
      onChange: _onChange,
    );
  }

  factory Bluetooth.fromStartupContext(StartupContext startupContext) {
    final bluetoothManager = bt.ControlProxy();
    startupContext.incoming.connectToService(bluetoothManager);
    return Bluetooth(monitor: bluetoothManager);
  }

  void _onChange() {
    spec = _specForBluetooth(_deviceNameList(model.remoteDevices));
  }

  @override
  void update(Value value) async {}

  @override
  void dispose() {
    model.dispose();
  }

  static Spec _specForBluetooth(List<TextValue> values) {
    if (values.isEmpty) {
      // No connected devices found. Send nullSpec to hide bluetooth settings
      return UiSpec.nullSpec;
    }
    return Spec(title: _title, groups: [
      Group(title: _title, values: [
        Value.withIcon(IconValue(codePoint: Icons.bluetooth.codePoint)),
        Value.withGrid(GridValue(columns: 1, values: values))
      ]),
    ]);
  }

  List<TextValue> _deviceNameList(List<bt.RemoteDevice> devices) {
    return devices.map((device) => TextValue(text: device.name)).toList();
  }
}

class BluetoothModel {
  final VoidCallback onChange;
  bt.ControlProxy _monitor;

  StreamSubscription _bluetoothSubscription;
  List<bt.RemoteDevice> _bluetoothDevices;

  BluetoothModel({
    @required this.onChange,
    bt.ControlProxy monitor,
  }) {
    _monitor = monitor ?? bt.ControlProxy();
    _bluetoothDevices = <bt.RemoteDevice>[];
    // Call initially to get devices connected at login.
    loadInitialBluetoothDevices();
    // Listen for device updates & check the connection status of bt devices
    _bluetoothSubscription = _monitor.onDeviceUpdated.listen((device) {
      listConnectedBluetoothDevices();
    });
  }

  void loadInitialBluetoothDevices() async {
    final remoteDevices = await _monitor.getKnownRemoteDevices();
    await Future.forEach(remoteDevices, (device) async {
      if (device != null && device.connected) {
        if (device.name != null) {
          _bluetoothDevices.add(device);
        }
      }
    });
    onChange.call();
  }

  void listConnectedBluetoothDevices() async {
    final remoteDevices = await _monitor.getKnownRemoteDevices();
    await Future.forEach(remoteDevices, (device) async {
      final deviceIdentifiers =
          _bluetoothDevices.map((btDevice) => btDevice.identifier);
      bool isPreviouslyConnectedDevice =
          deviceIdentifiers.contains(device.identifier);
      if (isPreviouslyConnectedDevice) {
        if (!device.connected) {
          _bluetoothDevices
              .removeWhere((dv) => dv.identifier == device.identifier);
          onChange?.call();
        }
      } else if (device != null && device.connected) {
        if (device.name != null) {
          _bluetoothDevices.add(device);
          onChange?.call();
        }
      }
    });
  }

  void dispose() {
    _bluetoothSubscription.cancel();
  }

  List<bt.RemoteDevice> get remoteDevices => _bluetoothDevices.toList();
}
