// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:fxtest/fxtest.dart';
import 'package:meta/meta.dart';

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

  /// Function that yields disposable wrappers around tests and [Process]
  /// instances.
  final TestRunner Function(TestsConfig) testRunnerBuilder;

  /// Helper which verifies that we actually want to run tests (not a dry run,
  /// for example) and are set up to even be successful (device and package
  /// server are available, for example).
  final Checklist checklist;

  int _numberOfTests;

  FuchsiaTestCommand({
    @required this.analyticsReporter,
    @required this.fuchsiaLocator,
    @required this.outputFormatter,
    @required this.checklist,
    @required this.testsConfig,
    @required this.testRunnerBuilder,
  }) : _numberOfTests = 0 {
    if (outputFormatter == null) {
      throw AssertionError('`outputFormatter` must not be null');
    }
    stream.listen(outputFormatter.update);
  }

  factory FuchsiaTestCommand.fromConfig(
    TestsConfig testsConfig, {
    @required TestRunner Function(TestsConfig) testRunnerBuilder,
    FuchsiaLocator fuchsiaLocator,
    OutputFormatter outputFormatter,
  }) {
    fuchsiaLocator = fuchsiaLocator ?? FuchsiaLocator.shared;
    var _outputFormatter =
        outputFormatter ?? OutputFormatter.fromConfig(testsConfig);
    return FuchsiaTestCommand(
      analyticsReporter: testsConfig.flags.dryRun
          ? AnalyticsReporter.noop()
          : AnalyticsReporter(
              fuchsiaLocator: fuchsiaLocator,
            ),
      checklist: PreChecker.fromConfig(
        testsConfig,
        eventSink: _outputFormatter.update,
      ),
      fuchsiaLocator: fuchsiaLocator,
      outputFormatter: _outputFormatter,
      testRunnerBuilder: testRunnerBuilder,
      testsConfig: testsConfig,
    );
  }

  Stream<TestEvent> get stream => _eventStreamController.stream;

  void emitEvent(TestEvent event) {
    if (!_eventStreamController.isClosed) {
      _eventStreamController.sink.add(event);
    }
  }

  void dispose() {
    _eventStreamController.close();
  }

  Future<void> runTestSuite(TestsManifestReader manifestReader) async {
    var parsedManifest = await readManifest(manifestReader);

    manifestReader.reportOnTestBundles(
      userFriendlyBuildDir: fuchsiaLocator.userFriendlyBuildDir,
      eventEmitter: emitEvent,
      parsedManifest: parsedManifest,
      testsConfig: testsConfig,
    );
    try {
      // Let the output formatter know that we're done parsing and
      // emitting preliminary events
      emitEvent(BeginningTests());
      await runTests(parsedManifest.testBundles);
    } on FailFastException catch (_) {
      // Non-zero exit code indicates generic but fatal failure
      exitCode = failureExitCode;
    }
    emitEvent(AllTestsCompleted());
  }

  Future<ParsedManifest> readManifest(
    TestsManifestReader manifestReader,
  ) async {
    List<TestDefinition> testDefinitions = await manifestReader.loadTestsJson(
      buildDir: fuchsiaLocator.buildDir,
      fxLocation: fuchsiaLocator.fx,
      manifestFileName: 'tests.json',
    );
    return manifestReader.aggregateTests(
      eventEmitter: emitEvent,
      matchLength: testsConfig.flags.matchLength,
      testBundleBuilder: testBundleBuilder,
      testDefinitions: testDefinitions,
      testsConfig: testsConfig,
    );
  }

  TestBundle testBundleBuilder(
    TestDefinition testDefinition, [
    double confidence,
  ]) =>
      TestBundle.build(
        confidence: confidence ?? 1,
        fxSuffix: fuchsiaLocator.fx,
        realtimeOutputSink: (String val) => emitEvent(TestOutputEvent(val)),
        timeElapsedSink: (Duration duration, String cmd, String output) =>
            emitEvent(TimeElapsedEvent(duration, cmd, output)),
        testRunnerBuilder: testRunnerBuilder,
        testDefinition: testDefinition,
        testsConfig: testsConfig,
        workingDirectory: fuchsiaLocator.buildDir,
      );

  Future<void> runTests(List<TestBundle> testBundles) async {
    // Enforce a limit
    var _testBundles = testsConfig.flags.limit > 0 &&
            testsConfig.flags.limit < testBundles.length
        ? testBundles.sublist(0, testsConfig.flags.limit)
        : testBundles;

    if (!await checklist.isDeviceReady(_testBundles)) {
      emitEvent(FatalError('Device is not ready for running device tests'));
      return;
    }

    for (TestBundle testBundle in _testBundles) {
      await testBundle.run().forEach((TestEvent event) {
        emitEvent(event);
        if (event is FatalError) {
          exitCode = failureExitCode;
        } else if (event is TestResult && !event.isSuccess) {
          exitCode = event.exitCode ?? failureExitCode;
        }
      });
      _numberOfTests += 1;
    }
  }

  /// Function guaranteed to be called at the end of execution, whether that is
  /// natural or the result of a SIGINT.
  Future<void> cleanUp() async {
    await _reportAnalytics();
  }

  Future<void> _reportAnalytics() async {
    final bool _actuallyRanTests = _numberOfTests != null && _numberOfTests > 0;
    if (!testsConfig.flags.dryRun && _actuallyRanTests) {
      await analyticsReporter.report(
        subcommand: 'test',
        action: 'number',
        label: '$_numberOfTests',
      );
    }
  }
}
