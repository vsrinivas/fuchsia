// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart' hide Intent;
import 'package:flutter_driver/driver_extension.dart';
import 'package:fuchsia_logger/logger.dart';

import 'src/inspect_integration_app.dart';

void main() {
  setupLogger(name: 'inspect-integration');
  enableFlutterDriverExtension();
  runApp(InspectIntegrationApp());
}
