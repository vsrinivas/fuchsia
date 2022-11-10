// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(http://fxbug.dev/107480): Resolve lint issues and reenable analysis for file
// ignore_for_file: deprecated_member_use

import 'package:ermine_utils/ermine_utils.dart' show CrashReportingRunner;
import 'package:flutter/material.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:shell_settings/src/states/settings_state.dart';
import 'package:shell_settings/src/utils/themes.dart';
import 'package:shell_settings/src/widgets/app.dart';

/// Main entry point to the shell settings module
Future<void> main() async {
  final runner = CrashReportingRunner();
  await runner.run(() async {
    setupLogger(name: 'shell_settings');

    final state = SettingsState.fromEnv();
    final app = App(state);

    // TODO(fxb/113485): Add stop & dispose calls for state
    state.start();

    runApp(
      MaterialApp(
        theme: AppTheme.darkTheme,
        home: Scaffold(
          body: Container(
            child: Center(
              child: app,
            ),
          ),
        ),
      ),
    );
  });
}
