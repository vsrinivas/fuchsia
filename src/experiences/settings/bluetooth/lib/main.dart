// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:lib.widgets/model.dart';

import 'src/bluetooth_model.dart';
import 'src/bluetooth_settings.dart';

/// Main entry point to the bluetooth settings module.
void main() {
  setupLogger();

  Widget app = MaterialApp(
    home: Container(
      child: ScopedModel<BluetoothSettingsModel>(
        model: BluetoothSettingsModel(),
        child: BluetoothSettings(),
      ),
    ),
  );

  runApp(app);
}
