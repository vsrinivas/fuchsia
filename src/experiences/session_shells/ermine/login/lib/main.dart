// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine_utils/ermine_utils.dart';
import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:login/src/states/oobe_state.dart';
import 'package:login/src/widgets/app.dart';
import 'package:login/src/widgets/ermine.dart';

Future<void> main() async {
  final runner = CrashReportingRunner();
  await runner.run(() async {
    setupLogger(name: 'login');
    final oobe = OobeState.fromEnv();
    final app = Observer(builder: (_) {
      return !oobe.ready
          ? Offstage()
          : oobe.loginDone
              ? ErmineApp(oobe)
              : oobe.launchOobe
                  ? OobeApp(oobe)
                  : Offstage();
    });
    runApp(app);
  });
}
