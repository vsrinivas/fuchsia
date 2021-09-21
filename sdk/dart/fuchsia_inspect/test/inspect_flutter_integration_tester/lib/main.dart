// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:flutter/material.dart' hide Intent;
import 'package:flutter_driver/driver_extension.dart';
import 'package:fuchsia_inspect/inspect.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart';

import 'src/inspect_integration_app.dart';

void main() {
  final context = ComponentContext.create();
  setupLogger(name: 'inspect-integration');
  enableFlutterDriverExtension();
  // Initialize & serve the inspect singleton before use in
  // InspectIntegrationApp.
  Inspect().serve(context.outgoing);
  context.outgoing.serveFromStartupInfo();
  runApp(InspectIntegrationApp());
}
