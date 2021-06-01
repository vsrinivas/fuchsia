// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine/src/models/app_model.dart' show AppModel;
import 'package:ermine/src/utils/crash.dart';
import 'package:ermine/src/widgets/app.dart' show App;
import 'package:flutter/material.dart';
import 'package:fuchsia_logger/logger.dart';

Future<void> main() async {
  final runner = CrashReportingRunner();
  await runner.run(() async {
    setupLogger(name: 'ermine');

    final model = AppModel();
    final app = App(model: model);

    runApp(app);

    await model.onStarted();
  });
}
