// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'collector.dart';

class EnvironmentCollector implements Collector {
  @override
  FutureOr<List<Item>> collect(bool includeSlow,
      {List<Category> restrictCategories}) {
    List<Item> result = new List<Item>();
    result.add(new Item(CategoryType.environmentInfo, 'build_dir',
        'Current build directory', Platform.environment['FUCHSIA_BUILD_DIR']));

    // if DEVICE_NAME was passed in fx -d, use it
    String deviceName = Platform.environment['FUCHSIA_DEVICE_NAME'];
    String notes = 'set by fx -d';
    if (deviceName == null) {
      // uses a file outside the build dir so that it is not removed by `gn clean`
      File deviceFile =
          new File('${Platform.environment['FUCHSIA_BUILD_DIR']}.device');
      if (deviceFile.existsSync()) {
        List<String> lines = deviceFile.readAsLinesSync();
        if (lines.isNotEmpty) {
          deviceName = lines[0];
          notes = 'set by `fx set-device`';
        }
      }
    }
    if (deviceName != null && deviceName.isNotEmpty) {
      result.add(new Item(CategoryType.environmentInfo, 'device_name',
          'Device name', deviceName, notes));
    }
    return result;
  }
}
