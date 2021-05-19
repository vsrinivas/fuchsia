// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:fuchsia_logger/logger.dart';

/// Main entry point to the license settings module.
void main() {
  setupLogger(name: 'license_settings');

  // TODO(fxb/76989): Setup license component for use in quicksettings
  Widget app = MaterialApp();

  runApp(app);
}
