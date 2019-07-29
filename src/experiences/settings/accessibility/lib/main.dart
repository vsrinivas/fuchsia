// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:lib.widgets/model.dart';

import 'src/accessibility_settings.dart';
import 'src/accessibility_settings_model.dart';

void main() {
  final AccessibilitySettingsModel model = AccessibilitySettingsModel();
  final Providers providers = Providers()..provideValue(model);
  final Widget app = ProviderNode(
    providers: providers,
    child: MaterialApp(home: AccessibilitySettings()),
  );

  runApp(app);
}
