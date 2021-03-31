// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart' hide Intent;
import 'package:fuchsia_inspect/inspect.dart' as inspect;
import 'package:fuchsia_logger/logger.dart';

import 'src/inspect_example_app.dart';

void main() {
  final context = ComponentContext.createAndServe();

  setupLogger(name: 'inspect-mod');
  final inspectNode = (inspect.Inspect()..serve(context.outgoing)).root;
  runApp(InspectExampleApp(inspectNode));
}
