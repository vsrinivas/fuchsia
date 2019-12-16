// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:meta/meta.dart';
import 'package:status/status.dart';

class DeviceData {
  final String deviceName;
  final String notes;
  DeviceData({this.deviceName, this.notes});

  @override
  String toString() => '<DeviceData {deviceName: $deviceName} {notes: $notes}>';
}

class DeviceFilenameReader {
  String getDeviceName({EnvReader envReader}) {
    String deviceName;
    // uses a file outside the build dir so that it is not removed by `gn clean`
    File deviceFile = File('${envReader.getEnv('FUCHSIA_BUILD_DIR')}.device');
    if (deviceFile.existsSync()) {
      List<String> lines = deviceFile.readAsLinesSync();
      if (lines.isNotEmpty) {
        deviceName = lines[0];
      }
    }
    return deviceName;
  }
}

class PreferredDeviceReader {
  DeviceData getDeviceName({
    @required EnvReader envReader,
    DeviceFilenameReader filenameReader,
  }) {
    // if DEVICE_NAME was passed in fx -d, use it
    String deviceName = envReader.getEnv('FUCHSIA_DEVICE_NAME');
    String notes = 'set by `fx -d`';
    if (deviceName == null) {
      filenameReader ??= DeviceFilenameReader();
      deviceName = filenameReader.getDeviceName(envReader: envReader);
      notes = deviceName != null ? 'set by `fx set-device`' : null;
    }
    return DeviceData(deviceName: deviceName, notes: notes);
  }
}

class EnvironmentCollector implements Collector {
  @override
  Future<List<Item>> collect({
    PreferredDeviceReader deviceReader,
    DeviceFilenameReader filenameReader,
    EnvReader envReader,
  }) {
    envReader ??= EnvReader.shared;
    List<Item> result = []..add(
        Item(
          CategoryType.environmentInfo,
          'build_dir',
          'Current build directory',
          envReader.getEnv('FUCHSIA_BUILD_DIR'),
        ),
      );

    deviceReader ??= PreferredDeviceReader();
    DeviceData deviceData = deviceReader.getDeviceName(
      filenameReader: filenameReader,
      envReader: envReader,
    );

    if (deviceData.deviceName != null && deviceData.deviceName.isNotEmpty) {
      result.add(Item(CategoryType.environmentInfo, 'device_name',
          'Device name', deviceData.deviceName, deviceData.notes));
    }
    return Future.value(result);
  }
}
