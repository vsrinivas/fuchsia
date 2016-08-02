// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import '../base/process.dart';
import 'modular_command.dart';

class CleanCommand extends ModularCommand {
  final String name = 'clean';
  final String description = 'Clean Modular';

  CleanCommand() {}

  @override
  Future<int> runInProject() {
    return process('rm', ['-rf', environment.buildDir]);
  }
}
