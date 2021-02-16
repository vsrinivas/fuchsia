// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'package:ermine/src/models/app_model.dart' show AppModel;
import 'package:ermine/src/utils/crash.dart';
import 'package:ermine/src/widgets/app.dart' show App;
import 'package:fuchsia_inspect/inspect.dart' show Inspect;
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart' show StartupContext;

Future<void> main() async {
  final runner = CrashReportingRunner();
  await runner.run(() async {
    StartupContext context = StartupContext.fromStartupInfo();
    setupLogger(name: 'ermine');

    // Initialize & serve the inspect singleton before use in AppModel.
    Inspect().serve(context.outgoing);
    final model = AppModel();
    final app = App(model: model);

    runApp(app);

    await model.onStarted();
  });
}
