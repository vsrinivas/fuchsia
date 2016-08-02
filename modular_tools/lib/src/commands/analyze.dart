// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import '../analyze.dart';
import 'modular_command.dart';

class AnalyzeCommand extends ModularCommand {
  final String name = 'analyze';
  final String description =
      'Runs a carefully configured dartanalyzer over the current project\'s'
      'dart code.';

  @override
  Future<int> runInProject() => new AnalyzerRunner(environment).runAnalyzer();
}
