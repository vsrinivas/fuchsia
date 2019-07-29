// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:lib.widgets/model.dart';

import 'model.dart';
import 'src/widget.dart';

/// Main entry point to the device settings module.
void main() {
  setupLogger();

  DeviceSettingsModel model = DeviceSettingsModel.withDefaultSystemInterface()
    ..start();

  Providers providers = Providers()..provideValue(model);

  Widget app = MaterialApp(
    home: Container(
      child: ProviderNode(
        providers: providers,
        child: DeviceSettings(),
      ),
    ),
  );

  runApp(app);
}
