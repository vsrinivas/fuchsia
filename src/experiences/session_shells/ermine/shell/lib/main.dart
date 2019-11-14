// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'package:fuchsia_logger/logger.dart';

import 'src/models/app_model.dart' show AppModel;
import 'src/widgets/app.dart' show App;

Future<void> main() async {
  setupLogger(name: 'ermine');

  final model = AppModel();
  final app = App(model: model);

  runApp(app);

  await model.onStarted();
}
