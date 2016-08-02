// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:args/command_runner.dart';
import 'package:path/path.dart' as path;

import '../configuration.dart';

abstract class ModularCommand extends Command {
  EnvironmentConfiguration environment;

  bool _warnedAboutAndroidFlag = false;
  TargetPlatform get target {
    if (globalResults['android']) {
      if (!_warnedAboutAndroidFlag) {
        stdout.writeln(
            'WARNING: --android flag is deprecated. Use --target=android');
        _warnedAboutAndroidFlag = true;
      }
      return TargetPlatform.android;
    }

    switch (globalResults['target']) {
      case 'android':
        return TargetPlatform.android;
      case 'linux':
        return TargetPlatform.linux;
      default:
        throw 'Not an allowed target platform';
    }
  }

  bool get release => globalResults['release'];

  Future<int> run() async {
    environment = new EnvironmentConfiguration();

    // Running outside the modular repo still requires a mojoconfig file.
    final String mojoconfigFilePath = path.join(environment.base, 'mojoconfig');
    final File mojoconfigFile = new File(mojoconfigFilePath);
    if (!(await mojoconfigFile.exists())) {
      throw 'Could not find mojoconfig file. Are you in the correct directory?';
    }

    return runInProject();
  }

  Future<int> runInProject();
}
