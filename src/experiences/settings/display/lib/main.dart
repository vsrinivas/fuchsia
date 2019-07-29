// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:lib.display.flutter/display_policy_brightness_model.dart';
import 'package:lib.widgets/model.dart';
import 'src/widget.dart';

/// Main entry point to the display settings module.
void main() {
  final Display display = Display();
  setupLogger();

  Widget app = MaterialApp(
    home: Container(
      child: ScopedModel<DisplayPolicyBrightnessModel>(
        model: DisplayPolicyBrightnessModel(display),
        child: DisplaySettings(),
      ),
    ),
  );

  runApp(app);
}
