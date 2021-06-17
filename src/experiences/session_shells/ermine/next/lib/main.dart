// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:fuchsia_logger/logger.dart';

import 'package:next/src/states/app_state.dart';
import 'package:next/src/utils/crash.dart';
import 'package:next/src/widgets/app.dart';

Future<void> main() async {
  final runner = CrashReportingRunner();
  await runner.run(() async {
    setupLogger(name: 'ermine');

    final state = AppState.fromEnv();
    final app = App(state);

    runApp(app);
  });
}
