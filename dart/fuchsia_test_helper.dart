// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports

import 'dart:async';
import 'dart:io';
import 'package:test/src/backend/declarer.dart';
import 'package:test/src/backend/group.dart';
import 'package:test/src/backend/suite_platform.dart';
import 'package:test/src/backend/operating_system.dart';
import 'package:test/src/backend/runtime.dart';
import 'package:test/src/runner/runner_suite.dart';
import 'package:test/src/runner/configuration/suite.dart';
import 'package:test/src/runner/engine.dart';
import 'package:test/src/runner/plugin/environment.dart';
import 'package:test/src/runner/reporter/expanded.dart';

/// Use `package:test` internals to run test functions.
///
/// `package:test` doesn't offer a public API for running tests. This calls
/// private APIs to invoke test functions and collect the results.
///
/// See: https://github.com/dart-lang/test/issues/48
///      https://github.com/dart-lang/test/issues/12
///      https://github.com/dart-lang/test/issues/99
Future<bool> runFuchsiaTests(List<Function> mainFunctions) async {
  final Declarer declarer = new Declarer();

  // TODO: use a nested declarer for each main?
  mainFunctions.forEach(declarer.declare);

  final Group group = declarer.build();

  final SuitePlatform platform = new SuitePlatform(
      Runtime.vm,
      os: OperatingSystem.findByIoName(Platform.operatingSystem),
      inGoogle: false);
  final RunnerSuite suite = new RunnerSuite(
      const PluginEnvironment(), SuiteConfiguration.empty, group,
      platform);

  final Engine engine = new Engine();
  engine.suiteSink.add(suite);
  engine.suiteSink.close();
  ExpandedReporter.watch(engine,
      color: false, printPath: false, printPlatform: false);

  return engine.run();
}
