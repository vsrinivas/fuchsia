// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

// ignore_for_file: implementation_imports

import 'dart:async';
import 'dart:io';

import 'package:fuchsia/fuchsia.dart' as fuchsia;
import 'package:test_api/src/backend/declarer.dart';
import 'package:test_api/src/backend/group.dart';
import 'package:test_api/src/backend/operating_system.dart';
import 'package:test_api/src/backend/runtime.dart';
import 'package:test_api/src/backend/suite_platform.dart';
import 'package:test_core/src/runner/configuration.dart';
import 'package:test_core/src/runner/engine.dart';
import 'package:test_core/src/runner/plugin/environment.dart';
import 'package:test_core/src/runner/reporter/expanded.dart';
import 'package:test_core/src/runner/runner_suite.dart';
import 'package:test_core/src/util/exit_codes.dart' as exit_codes;
import 'package:test_core/src/util/io.dart';
import 'package:test_core/src/util/print_sink.dart';

typedef MainFunction = void Function();

// This is copied from package:test_core/src/runner.dart:_loadSuites().
RunnerSuite _applyFilter(RunnerSuite suite) {
  return suite.filter((test) {
    // Skip any tests that don't match all the given patterns.
    if (!suite.config.patterns
        .every((pattern) => test.name.contains(pattern))) {
      return false;
    }

    // If the user provided tags, skip tests that don't match all of them.
    if (!suite.config.includeTags.evaluate(test.metadata.tags.contains)) {
      return false;
    }

    // Skip tests that do match any tags the user wants to exclude.
    if (suite.config.excludeTags.evaluate(test.metadata.tags.contains)) {
      return false;
    }

    return true;
  });
}

// This is adapted from package:test_core/src/executable.dart.
/// Print usage information for this command.
///
/// If [error] is passed, it's used in place of the usage message and the whole
/// thing is printed to stderr instead of stdout.
void _printUsage([String error]) {
  if (error != null) {
    stderr.write('''${wordWrap(error)}

${Configuration.usage}
''');
  } else {
    stdout.writeln(Configuration.usage);
  }
}

/// Use `package:test` internals to run test functions.
///
/// `package:test` doesn't offer a public API for running tests. This calls
/// private APIs to invoke test functions and collect the results.
///
/// See: https://github.com/dart-lang/test/issues/48
///      https://github.com/dart-lang/test/issues/12
///      https://github.com/dart-lang/test/issues/99
Future<int> runFuchsiaTests(
    List<MainFunction> mainFunctions, List<String> args) async {
  Configuration configuration;
  try {
    configuration = Configuration.parse(args);
  } on FormatException catch (error) {
    _printUsage(error.message);
    return exit_codes.usage;
  }

  if (configuration.help) {
    _printUsage();
    return 0;
  }

  return configuration.asCurrent(() async {
    final declarer = Declarer();

    // TODO: use a nested declarer for each main?
    mainFunctions.forEach(declarer.declare);

    final Group group = declarer.build();

    final platform = SuitePlatform(Runtime.vm,
        os: OperatingSystem.findByIoName(Platform.operatingSystem),
        inGoogle: false);
    final suite = _applyFilter(RunnerSuite(const PluginEnvironment(),
        configuration.suiteDefaults, group, platform));

    final engine = Engine();
    engine.suiteSink.add(suite);
    engine.suiteSink.close();
    ExpandedReporter.watch(engine, PrintSink(),
        color: false, printPath: false, printPlatform: false);

    return await engine.run() ? 0 : 1;
  });
}

void exitFuchsiaTest(int returnCode) {
  fuchsia.exit(returnCode);
}
