// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:path/path.dart' as path;

import '../configuration.dart';
import '../refresh.dart';
import 'modular_command.dart';

class RefreshCommand extends ModularCommand {
  final String name = 'refresh';
  final String description =
      'Refreshes a running module with new binary updates.';

  RefreshCommand() {
    argParser.addOption('output-name',
        help: 'output name of the module to reload.');
  }

  @override
  Future<int> runInProject() async {
    final List<ProjectConfiguration> projects =
        await environment.projectConfigurations.toList();
    if (argResults['output-name'] == null &&
        (projects.isEmpty ||
            projects.length != 1 ||
            !(await new File(
                    path.join(projects[0].projectRoot, 'manifest.yaml'))
                .exists()))) {
      print(
          'Either run from a subdirectory with single pubspec.yaml and manifest.yaml or specify \'--output-name\' to refresh');
      return 1;
    }

    return await new RefreshCommandRunner(environment, release)
        .refresh(argResults['output-name']);
  }
}
