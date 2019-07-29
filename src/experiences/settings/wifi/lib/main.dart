// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:lib.widgets/model.dart';

import 'src/fuchsia/wifi_settings_model.dart';
import 'src/wlan_manager.dart';

/// Main entry point to the wifi settings module.
void main() {
  setupLogger(name: 'wifi_settings');

  Widget app = MaterialApp(
    home: Container(
      child: ScopedModel<WifiSettingsModel>(
        model: WifiSettingsModel(),
        child: WlanManager(),
      ),
    ),
  );

  runApp(app);
}
