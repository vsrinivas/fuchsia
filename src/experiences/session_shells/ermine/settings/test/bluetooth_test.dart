// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl_fuchsia_bluetooth_control/fidl_async.dart' as bt;
import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mockito/mockito.dart';
import 'package:settings/settings.dart';
import 'package:internationalization/strings.dart';
import 'package:quickui/quickui.dart';

void main() {
  String _disconnect = Strings.disconnect.toUpperCase();

  test('Default Bluetooth Spec', () async {
    final control = MockControl();

    // Add mock bluetooth devices
    List<bt.RemoteDevice> mockBluetoothDevices = <bt.RemoteDevice>[]
      ..add(_buildMockDevice('Mouse', '1111'))
      ..add(_buildMockDevice('Keyboard', '2222'))
      ..add(_buildMockDevice('Headphones', '3333'));

    when(control.getKnownRemoteDevices())
        .thenAnswer((response) => Future.value(mockBluetoothDevices));
    when(control.onDeviceUpdated).thenAnswer(
        (response) => Stream.value(_buildMockDevice('mock', '0000')));

    Bluetooth bluetooth = Bluetooth(monitor: control);

    // Should receive bluetooth spec.
    Spec spec = await bluetooth.getSpec();
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);

    // Confirm grid values are correct
    GridValue grid = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.grid)
        .first
        ?.grid;

    List<String> mockBtNames =
        grid.values.map((device) => device.text).toList();
    expect(mockBtNames[0], 'Mouse');
    expect(mockBtNames[1], _disconnect);
    expect(mockBtNames[2], 'Keyboard');
    expect(mockBtNames[3], _disconnect);
    expect(mockBtNames[4], 'Headphones');
    expect(mockBtNames[5], _disconnect);

    // Confirm bluetooth icon present
    bool hasIcon = spec.groups.first.values.any((v) => v.$tag == ValueTag.icon);
    expect(hasIcon, isTrue);

    bluetooth.dispose();
  });

  test('Add Bluetooth Device', () async {
    final control = MockControl();

    // Add initial mock bluetooth devices
    List<bt.RemoteDevice> mockBluetoothDevices = <bt.RemoteDevice>[]
      ..add(_buildMockDevice('Mouse', '1111'));

    when(control.getKnownRemoteDevices())
        .thenAnswer((response) => Future.value(mockBluetoothDevices));
    when(control.onDeviceUpdated).thenAnswer(
        (response) => Stream.value(_buildMockDevice('mock', '0000')));

    Bluetooth bluetooth = Bluetooth(monitor: control);

    // Should receive bluetooth spec.
    Spec spec = await bluetooth.getSpec();
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);

    // Confirm initial grid values are correct
    GridValue grid = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.grid)
        .first
        ?.grid;

    List<String> mockBtNames =
        grid.values.map((device) => device.text).toList();
    expect(mockBtNames[0], 'Mouse');
    expect(mockBtNames[1], _disconnect);

    // Confirm bluetooth icon present
    bool hasIcon = spec.groups.first.values.any((v) => v.$tag == ValueTag.icon);
    expect(hasIcon, isTrue);

    // Add devices to mock bluetooth devices
    mockBluetoothDevices
      ..add(_buildMockDevice('Keyboard', '2222'))
      ..add(_buildMockDevice('Headphones', '3333'));
    bluetooth.model.remoteDevices = mockBluetoothDevices;

    // Get updated spec
    spec = await bluetooth.getSpec();
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);

    // Confirm updated grid values are correct
    grid = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.grid)
        .first
        ?.grid;

    mockBtNames = grid.values.map((device) => device.text).toList();
    expect(mockBtNames[0], 'Mouse');
    expect(mockBtNames[1], _disconnect);
    expect(mockBtNames[2], 'Keyboard');
    expect(mockBtNames[3], _disconnect);
    expect(mockBtNames[4], 'Headphones');
    expect(mockBtNames[5], _disconnect);

    bluetooth.dispose();
  });

  test('Remove Bluetooth Device', () async {
    final control = MockControl();

    // Add mock bluetooth devices
    List<bt.RemoteDevice> mockBluetoothDevices = <bt.RemoteDevice>[]
      ..add(_buildMockDevice('Mouse', '1111'))
      ..add(_buildMockDevice('Keyboard', '2222'))
      ..add(_buildMockDevice('Headphones', '3333'));

    when(control.getKnownRemoteDevices())
        .thenAnswer((response) => Future.value(mockBluetoothDevices));
    when(control.onDeviceUpdated).thenAnswer(
        (response) => Stream.value(_buildMockDevice('mock', '0000')));

    Bluetooth bluetooth = Bluetooth(monitor: control);

    // Should receive bluetooth spec.
    Spec spec = await bluetooth.getSpec();
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);

    // Confirm grid values are correct
    GridValue grid = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.grid)
        .first
        ?.grid;

    List<String> mockBtNames =
        grid.values.map((device) => device.text).toList();
    expect(mockBtNames[0], 'Mouse');
    expect(mockBtNames[1], _disconnect);
    expect(mockBtNames[2], 'Keyboard');
    expect(mockBtNames[3], _disconnect);
    expect(mockBtNames[4], 'Headphones');
    expect(mockBtNames[5], _disconnect);

    // Confirm bluetooth icon present
    bool hasIcon = spec.groups.first.values.any((v) => v.$tag == ValueTag.icon);
    expect(hasIcon, isTrue);

    // Remove device from mock bluetooth devices
    mockBluetoothDevices.removeWhere((device) => device.identifier == '1111');
    bluetooth.model.remoteDevices = mockBluetoothDevices;

    // Get updated spec
    spec = await bluetooth.getSpec();
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);

    // Confirm updated grid values are correct
    grid = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.grid)
        .first
        ?.grid;

    mockBtNames = grid.values.map((device) => device.text).toList();
    expect(mockBtNames[0], 'Keyboard');
    expect(mockBtNames[1], _disconnect);
    expect(mockBtNames[2], 'Headphones');
    expect(mockBtNames[3], _disconnect);

    // Remove another device from mock bluetooth devices
    mockBluetoothDevices.removeWhere((device) => device.identifier == '3333');
    bluetooth.model.remoteDevices = mockBluetoothDevices;

    // Get updated spec
    spec = await bluetooth.getSpec();
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);

    // Confirm updated grid values are correct
    grid = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.grid)
        .first
        ?.grid;

    mockBtNames = grid.values.map((device) => device.text).toList();
    expect(mockBtNames[0], 'Keyboard');
    expect(mockBtNames[1], _disconnect);

    // Remove last device from mock bluetooth devices
    mockBluetoothDevices.removeWhere((device) => device.identifier == '2222');
    bluetooth.model.remoteDevices = mockBluetoothDevices;

    // Get updated spec & confirm it is nullSpec
    spec = await bluetooth.getSpec();
    expect(spec, UiSpec.nullSpec);

    bluetooth.dispose();
  });

  test('Empty Bluetooth Spec', () async {
    final control = MockControl();

    // Add mock bluetooth devices
    List<bt.RemoteDevice> mockBluetoothDevices = <bt.RemoteDevice>[];

    when(control.getKnownRemoteDevices())
        .thenAnswer((response) => Future.value(mockBluetoothDevices));
    when(control.onDeviceUpdated).thenAnswer(
        (response) => Stream.value(_buildMockDevice('mock', '0000')));

    Bluetooth bluetooth = Bluetooth(monitor: control);

    // Should receive bluetooth spec as nullSpec.
    Spec spec = await bluetooth.getSpec();
    expect(spec, UiSpec.nullSpec);

    bluetooth.dispose();
  });
}

bt.RemoteDevice _buildMockDevice(String name, String id) {
  List<String> mockServiceUuids = ['mockUuid1'];
  return bt.RemoteDevice(
    name: name,
    identifier: id,
    bonded: true,
    technology: bt.TechnologyType.lowEnergy,
    serviceUuids: mockServiceUuids,
    connected: true,
    address: 'mockAddress',
    appearance: bt.Appearance.unknown,
  );
}

class MockControl extends Mock implements bt.ControlProxy {}
