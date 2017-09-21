// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:lib.app.dart/app.dart';
import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';
import 'package:lib.logging/logging.dart';
import 'package:lib.widgets/modular.dart';
import 'src/modular/module_model.dart';
import 'src/screen.dart';

void main() {
  setupLogger();

  ApplicationContext appContext = new ApplicationContext.fromStartupInfo();

  ModuleWidget<SettingsModuleModel> moduleWidget =
      new ModuleWidget<SettingsModuleModel>(
          moduleModel: new SettingsModuleModel(appContext),
          applicationContext: appContext,
          child: new SettingsScreen());

  moduleWidget.advertise();

  runApp(new MaterialApp(home: moduleWidget));
}
