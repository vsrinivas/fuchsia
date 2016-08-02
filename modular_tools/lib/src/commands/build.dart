// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import '../build.dart';
import 'modular_command.dart';

class BuildCommand extends ModularCommand {
  final String name = 'build';
  final String description = 'Build modules.';

  @override
  Future<int> runInProject() =>
      new BuildRunner(environment, release).runBuild();
}
