// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fxtest/fxtest.dart';
import 'package:meta/meta.dart';
import 'package:pedantic/pedantic.dart';

/// Main entry-point for all Fuchsia tests, both host and on-device.
///
/// [FuchsiaTestCommand] receives a combination of test names and feature flags,
/// and due to the nature of Fuchsia tests, needs to run each passed test name
/// against the full set of feature flags. For example, if the way to invoke
/// this command is `fx test <...args>`, and the developer executes
/// `... test1 test2 -flag1 -flag2`, the desired behavior for our user lies
/// behind pretending the user entered `... test1 -flag1 -flag2` and
/// `... test2 -flag1 flag2` separately. Note that the individual tests indicated
/// by these two imaginary commands are then rolled back into one singular test
/// suite, so end users will be none the wiser.
///
/// The following is a high level road map of how [FuchsiaTestCommand] works:
///
///  - Parse commands into testNames and flags
///  - Produce a list of each individual `testName` paired with all provided flags
///     - loop over this list `testName` and flag combinations, matching tests
///       out of `out/default/tests.json`
///     - Load each matching test's raw JSON into a [TestDefinition] instance which
///       handles determining all the runtime invocation details for a given test
///  - Loop over the list of [TestDefinition] instances, listening to emitted
///    [TestEvent] instances with a [FormattedOutput] class
///     - Every time a [TestEvent] instance is emitted, our [FormattedOutput]
///       class captures it and flushes appropriate text to [stdout].
///  - Once the [TestDefinition] list has been completely processed, flush any
///    captured stderr from individual test processes to show errors / stacktraces
class FuchsiaTestCommand {
  // ignore: close_sinks
  final _eventStreamController = StreamController<TestEvent>();

  final AnalyticsReporter analyticsReporter;

  /// Bundle of configuration options for this invocation.
  final TestsConfig testsConfig;

  /// Absolute path of the active Fuchsia build.
  final FuchsiaLocator fuchsiaLocator;

  /// Translator between [TestEvent] instances and output for the user.
  final OutputFormatter outputFormatter;

  int _numberOfTests;

  FuchsiaTestCommand({
    @required this.analyticsReporter,
    @required this.fuchsiaLocator,
    @required this.outputFormatter,
    @required this.testsConfig,
  }) : _numberOfTests = 0;

  Stream<TestEvent> get stream => _eventStreamController.stream;

  void emitEvent(TestEvent event) {
    _eventStreamController.sink.add(event);
  }

  void dispose() {
    _eventStreamController.sink.close();
  }

  Future<int> runTestSuite({TestsManifestReader manifestReader}) async {
    stream.listen(outputFormatter.update);
    var parsedManifest = await parseManifest(manifestReader);
    manifestReader.reportOnTestBundles(
      userFriendlyBuildDir: fuchsiaLocator.userFriendlyBuildDir,
      eventEmitter: emitEvent,
      parsedManifest: parsedManifest,
      testsConfig: testsConfig,
    );
    var exitCode = 0;

    try {
      // Let the output formatter know that we're done parsing and
      // emitting preliminary events
      emitEvent(BeginningTests());
      await runTests(parsedManifest.testBundles).forEach((event) {
        emitEvent(event);
        if (event is TestResult && !event.isSuccess) {
          exitCode = 2;
        }
      });
    } on FailFastException catch (_) {
      exitCode = 2;
    }
    emitEvent(AllTestsCompleted());
    return exitCode;
  }

  Future<ParsedManifest> parseManifest(
    TestsManifestReader manifestReader,
  ) async {
    // ignore: parameter_assignments
    manifestReader ??= TestsManifestReader();
    List<TestDefinition> testDefinitions = await manifestReader.loadTestsJson(
      buildDir: fuchsiaLocator.buildDir,
      manifestFileName: 'tests.json',
    );
    return manifestReader.aggregateTests(
      buildDir: fuchsiaLocator.buildDir,
      eventEmitter: emitEvent,
      testDefinitions: testDefinitions,
      testsConfig: testsConfig,
    );
  }

  Stream<TestEvent> runTests(List<TestBundle> testBundles) async* {
    var count = 0;
    for (TestBundle testBundle in testBundles) {
      yield* testBundle.run();

      count += 1;
      _numberOfTests = count;
      if (testsConfig.flags.limit > 0 && count >= testsConfig.flags.limit) {
        break;
      }
    }
  }

  /// Function guaranteed to be called at the end of execution, whether that is
  /// natural or the result of a SIGINT.
  Future<void> cleanUp() async {
    await _reportAnalytics();
  }

  Future<void> _reportAnalytics() async {
    final _actuallyRanTests = _numberOfTests != null && _numberOfTests > 0;
    if (!testsConfig.flags.dryRun && _actuallyRanTests) {
      await analyticsReporter.report(
        subcommand: 'test',
        action: 'number',
        label: '$_numberOfTests',
      );
    }
  }
}
