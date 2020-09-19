// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:fxtest/fxtest.dart';
import 'package:io/ansi.dart';
import 'package:meta/meta.dart';

import 'exit_code.dart';

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

  /// Translators between [TestEvent] instances and output for the user.
  final List<OutputFormatter> outputFormatters;

  /// Function that yields disposable wrappers around tests and [Process]
  /// instances.
  final TestRunner Function(TestsConfig) testRunnerBuilder;

  /// Helper which verifies that we actually want to run tests (not a dry run,
  /// for example) and are set up to even be successful (device and package
  /// server are available, for example).
  final Checklist checklist;

  final ExitCodeSetter _exitCodeSetter;

  /// Used to create any new directories needed to house test output / artifacts.
  final DirectoryBuilder directoryBuilder;

  int _numberOfTests;

  FuchsiaTestCommand({
    @required this.analyticsReporter,
    @required this.outputFormatters,
    @required this.checklist,
    @required this.testsConfig,
    @required this.testRunnerBuilder,
    @required this.directoryBuilder,
    ExitCodeSetter exitCodeSetter,
  })  : _exitCodeSetter = exitCodeSetter ?? setExitCode,
        _numberOfTests = 0 {
    if (outputFormatters == null || outputFormatters.isEmpty) {
      throw AssertionError('Must provide at least one OutputFormatter');
    }
    stream.listen((output) {
      for (var formatter in outputFormatters) {
        formatter.update(output);
      }
    });
  }

  factory FuchsiaTestCommand.fromConfig(
    TestsConfig testsConfig, {
    @required TestRunner Function(TestsConfig) testRunnerBuilder,
    DirectoryBuilder directoryBuilder,
    ExitCodeSetter exitCodeSetter,
    OutputFormatter outputFormatter,
  }) {
    var _outputFormatter =
        outputFormatter ?? OutputFormatter.fromConfig(testsConfig);
    var _fileFormatter = FileFormatter.fromConfig(testsConfig);
    return FuchsiaTestCommand(
      analyticsReporter: testsConfig.flags.dryRun
          ? AnalyticsReporter.noop()
          : AnalyticsReporter(fxEnv: testsConfig.fxEnv),
      checklist: PreChecker.fromConfig(
        testsConfig,
        eventSink: _outputFormatter.update,
      ),
      directoryBuilder: directoryBuilder ?? (path, {recursive}) => null,
      outputFormatters: [
        _outputFormatter,
        if (_fileFormatter != null) _fileFormatter
      ],
      testRunnerBuilder: testRunnerBuilder,
      testsConfig: testsConfig,
      exitCodeSetter: exitCodeSetter,
    );
  }

  Stream<TestEvent> get stream => _eventStreamController.stream;

  void emitEvent(TestEvent event) {
    if (!_eventStreamController.isClosed) {
      _eventStreamController.sink.add(event);
    }
  }

  void dispose() {
    for (var formatter in outputFormatters) {
      formatter.close();
    }
    _eventStreamController.close();
  }

  Future<void> runTestSuite([TestsManifestReader manifestReader]) async {
    manifestReader ??= TestsManifestReader();
    advertiseLogFile();
    var parsedManifest = await readManifest(manifestReader);

    manifestReader.reportOnTestBundles(
      userFriendlyBuildDir: testsConfig.fxEnv.userFriendlyOutputDir,
      eventEmitter: emitEvent,
      parsedManifest: parsedManifest,
      testsConfig: testsConfig,
    );

    if (parsedManifest.testBundles.isEmpty) {
      return noMatchesHelp(
        manifestReader: manifestReader,
        testDefinitions: parsedManifest.testDefinitions,
        testsConfig: testsConfig,
      );
    }

    if (testsConfig.flags.shouldRandomizeTestOrder) {
      parsedManifest.testBundles.shuffle();
    }

    if (testsConfig.flags.shouldRebuild) {
      Set<String> buildTargets =
          TestBundle.calculateMinimalBuildTargets(parsedManifest.testBundles);
      emitEvent(TestInfo(testsConfig.wrapWith(
          '> fx build ${buildTargets?.join(' ') ?? ''}', [green, styleBold])));
      try {
        await fxCommandRun(
            testsConfig.fxEnv.fx, 'build', buildTargets?.toList());
      } on FxRunException {
        emitEvent(FatalError(
            '\'fx test\' could not perform a successful build. Try to run \'fx build\' manually or use the \'--no-build\' flag'));
        _exitCodeSetter(failureExitCode);
        return;
      }
      // Re-parse the manifest in case it has changed as a side effect of building
      parsedManifest = await readManifest(manifestReader);
    }

    try {
      // Let the output formatter know that we're done parsing and
      // emitting preliminary events
      emitEvent(BeginningTests());
      await runTests(parsedManifest.testBundles);
    } on FailFastException catch (_) {
      // Non-zero exit code indicates generic but fatal failure
      _exitCodeSetter(failureExitCode);
    }
    emitEvent(AllTestsCompleted());
  }

  Future<ParsedManifest> readManifest(
    TestsManifestReader manifestReader,
  ) async {
    List<TestDefinition> testDefinitions = await manifestReader.loadTestsJson(
      buildDir: testsConfig.fxEnv.outputDir,
      fxLocation: testsConfig.fxEnv.fx,
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

  void noMatchesHelp({
    @required TestsManifestReader manifestReader,
    @required List<TestDefinition> testDefinitions,
    @required TestsConfig testsConfig,
  }) {
    emitEvent(GeneratingHintsEvent());
    emitEvent(TestInfo(
      testsConfig.wrapWith(
          'Could not find any tests to run with the '
          'arguments you provided.',
          [lightYellow]),
    ));
    var manifestOfHints = manifestReader.aggregateTests(
      comparer: FuzzyComparer(threshold: testsConfig.flags.fuzzyThreshold),
      eventEmitter: (TestEvent event) => null,
      matchLength: testsConfig.flags.matchLength,
      testBundleBuilder: testBundleBuilder,
      testDefinitions: testDefinitions,
      testsConfig: testsConfig,
    );
    if (manifestOfHints.testBundles.isNotEmpty) {
      manifestOfHints.testBundles.sort(
        (TestBundle bundle1, TestBundle bundle2) {
          return bundle1.confidence.compareTo(bundle2.confidence);
        },
      );
      var hints = manifestOfHints.testBundles.length > 1 ? 'hints' : 'hint';
      emitEvent(TestInfo(
        'Did you mean... (${manifestOfHints.testBundles.length} $hints)?',
        requiresPadding: false,
      ));
      for (TestBundle bundle in manifestOfHints.testBundles) {
        emitEvent(TestInfo(
          ' -- ${bundle.testDefinition.name}',
          requiresPadding: false,
        ));
      }
    } else {
      emitEvent(TestInfo(
        'Make sure this test is transitively in your \'fx set\' arguments. See https://fuchsia.dev/fuchsia-src/development/testing/faq for more information.',
        requiresPadding: false,
      ));
    }
  }

  TestBundle testBundleBuilder(
    TestDefinition testDefinition, [
    double confidence,
  ]) =>
      TestBundle.build(
        confidence: confidence ?? 1,
        directoryBuilder: directoryBuilder,
        fxPath: testsConfig.fxEnv.fx,
        realtimeOutputSink: (String val) => emitEvent(TestOutputEvent(val)),
        timeElapsedSink: (Duration duration, String cmd, String output) =>
            emitEvent(TimeElapsedEvent(duration, cmd, output)),
        testRunnerBuilder: testRunnerBuilder,
        testDefinition: testDefinition,
        testsConfig: testsConfig,
        workingDirectory: testsConfig.fxEnv.outputDir,
      );

  Future<void> runTests(List<TestBundle> testBundles) async {
    // Enforce a limit
    var _testBundles = testsConfig.flags.limit > 0 &&
            testsConfig.flags.limit < testBundles.length
        ? testBundles.sublist(0, testsConfig.flags.limit)
        : testBundles;

    if (!await checklist.isDeviceReady(_testBundles)) {
      emitEvent(FatalError('Device is not ready for running device tests'));
      _exitCodeSetter(failureExitCode);
      return;
    }

    // set merkle root hash on component tests
    // TODO: This should not require checking if `buildDir != null`, as that is
    // a temporary workaround to get tests passing on CQ. The correct
    // implementation is to abstract file-reading just as we have process-launching.
    PackageRepository packageRepository;
    if (testsConfig.flags.shouldUsePackageHash &&
        testsConfig.fxEnv.outputDir != null) {
      packageRepository = await PackageRepository.fromManifest(
          buildDir: testsConfig.fxEnv.outputDir);
    }

    for (TestBundle testBundle in _testBundles) {
      if (packageRepository != null &&
          testBundle.testDefinition.packageUrl != null) {
        String packageName = testBundle.testDefinition.packageUrl.packageName;
        testBundle.testDefinition.hash = packageRepository[packageName].merkle;
      }
      await testBundle.run().forEach((TestEvent event) {
        emitEvent(event);
        if (event is FatalError) {
          _exitCodeSetter(failureExitCode);
        } else if (event is TestResult && !event.isSuccess) {
          _exitCodeSetter(event.exitCode ?? failureExitCode);
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

  void advertiseLogFile() {
    for (var outputFormatter in outputFormatters) {
      if (outputFormatter is FileFormatter) {
        // ignore: avoid_as
        (outputFormatter.buffer.stdout as FileStandardOut).initPath();
        // ignore: avoid_as
        var path = (outputFormatter.buffer.stdout as FileStandardOut).path;
        emitEvent(TestInfo(testsConfig.wrapWith(
          'Logging all output to: $path\n'
          'Use the `--logpath` argument to specify a log location or '
          '`--no-log` to disable\n',
          [darkGray],
        )));
      }
    }
  }
}
