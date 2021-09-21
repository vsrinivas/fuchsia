// 15 Puzzle on Torus - June 2019
// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_modular/module.dart';

void main() {
  setupLogger(name: 'torus15');

  // launch app
  Module();
}
