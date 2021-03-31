// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:fuchsia_logger/logger.dart';

import 'src/widgets/app.dart';

void main() {
  setupLogger(name: 'slider_mod');
  runApp(MaterialApp(home: App()));
}
