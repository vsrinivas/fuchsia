// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:settings/src/models/settings_model.dart'; // ignore: implementation_imports
import 'package:test/test.dart';

class TestSettingsModel extends SettingsModel {
  @override
  void initialize() {}
}

void main() {
  TestSettingsModel _settingsModel = TestSettingsModel();

  test('test buildInfo - debug build', () {
    final DateTime testDate = DateTime.utc(2006, 10, 6, 13, 20, 0);
    _settingsModel.testBuildTag = testDate.toString();
    expect(_settingsModel.buildInfo, equals('2006-10-06 13:20:00.000Z'));
  });

  test('test buildInfo - release build', () {
    _settingsModel.testBuildTag = '20190422_00_RC01';
    expect(_settingsModel.buildInfo, equals('20190422_00_RC01'));
  });
}
