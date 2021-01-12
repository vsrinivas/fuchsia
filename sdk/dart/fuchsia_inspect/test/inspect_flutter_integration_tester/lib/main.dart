// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fuchsia_logger/logger.dart';
import 'package:flutter/material.dart' hide Intent;

import 'src/inspect_integration_app.dart';

void main() {
  setupLogger(name: 'inspect-integration');
  runApp(InspectIntegrationApp());
}
