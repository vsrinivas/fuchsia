// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Helper code for setting up SL4F, running performance tests, and
// uploading the tests' results to the Catapult performance dashboard.

import 'dart:io' show File, Platform;

import 'package:args/args.dart';
import 'package:logging/logging.dart';
import 'package:meta/meta.dart';
import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:sl4f/trace_processing.dart';
import 'package:test/test.dart';

import 'summarize.dart' show summarizeFuchsiaPerfFiles, writeFuchsiaPerfJson;

void enableLoggingOutput() {
  // This is necessary to get information about the commands the tests have
  // run, and to get information about what they outputted on stdout/stderr
  // if they fail.
  Logger.root
    ..level = Level.ALL
    ..onRecord.listen((rec) => print('[${rec.level}]: ${rec.message}'));
}

void runShardTests(List<String> args, List<void Function()> tests) {
  // The Dart test library is supposed to support sharding, but its
  // sharding options do not seem to be accessible when running Dart tests
  // on Fuchsia, so we reimplement the same options here.
  final parser = ArgParser()
    ..addOption('total-shards',
        help: 'Number of total shards to split test suites into.',
        defaultsTo: '1')
    ..addOption('shard-index',
        help: 'Which shard of test suites to run.', defaultsTo: '0');
  final argResults = parser.parse(args);

  int totalShards = int.parse(argResults['total-shards']);
  int shardIndex = int.parse(argResults['shard-index']);
  for (var i = 0; i < tests.length; i++) {
    if (i % totalShards == shardIndex) {
      tests[i]();
    }
  }
}

class PerfTestHelper {
  // Pathname to which components run via runTestComponent() should
  // write their results.
  static const String componentOutputPath = '/tmp/results.fuchsiaperf.json';

  sl4f.Sl4f sl4fDriver;
  sl4f.Performance performance;
  sl4f.Dump dump;
  sl4f.Storage storage;
  sl4f.Component component;

  Future<void> setUp() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
    addTearDown(() async {
      await sl4fDriver.stopServer();
      sl4fDriver.close();
    });
    performance = sl4f.Performance(sl4fDriver);
    dump = sl4f.Dump();
    storage = sl4f.Storage(sl4fDriver);
    component = sl4f.Component(sl4fDriver);
  }

  static Future<PerfTestHelper> make() async {
    final helper = PerfTestHelper();
    await helper.setUp();
    return helper;
  }

  // Takes a fuchsiaperf file, specified as a remote filename
  // (i.e. the path of the file on the Fuchsia device under test).
  // Publishes this file as results for the current test.
  Future<void> processResults(String resultsFile) async {
    final localResultsFile =
        await storage.dumpFile(resultsFile, 'results', 'fuchsiaperf.json');

    await performance.convertResults('runtime_deps/catapult_converter',
        localResultsFile, Platform.environment);
  }

  // Takes a set of "raw data" fuchsiaperf files, specified as local
  // files.  Generates a "summary" version of that data, following the
  // process described in summarize.dart, and publishes that as
  // results for the current test.
  Future<void> processResultsSummarized(List<File> jsonFiles) async {
    final jsonSummaryData = summarizeFuchsiaPerfFiles(jsonFiles);

    final File jsonSummaryFile = dump.createFile('results', 'fuchsiaperf.json');
    await writeFuchsiaPerfJson(jsonSummaryFile, jsonSummaryData);

    await performance.convertResults('runtime_deps/catapult_converter',
        jsonSummaryFile, Platform.environment);
  }

  // Runs a command over SSH and publishes its output as performance
  // test results.
  //
  // The command to run is specified via a function that takes a
  // filename as an argument and returns a shell command string.  The
  // filename is for the results file that the command will write its
  // results to, in fuchsiaperf.json format.
  Future<void> runTestCommand(
      String Function(String resultsFilename) getCommand) async {
    // Make a filename that is very likely to be unique.  Using a
    // unique filename should not be strictly necessary, but it should
    // avoid potential problems.  We do not expect performance tests
    // to be run concurrently on the Infra builders, but it may be
    // useful to do so locally for development purposes when we don't
    // care about the performance results.
    final timestamp = DateTime.now().microsecondsSinceEpoch;
    final resultsFile = '/tmp/perf_results_$timestamp.fuchsiaperf.json';
    final command = getCommand(resultsFile);
    final result = await sl4fDriver.ssh.run(command);
    expect(result.exitCode, equals(0));
    try {
      final File localResultsFile = await storage.dumpFile(
          resultsFile, 'results', 'fuchsiaperf_full.json');
      await processResultsSummarized([localResultsFile]);
    } finally {
      // Clean up: remove the temporary file.
      final result = await sl4fDriver.ssh.run('rm -f $resultsFile');
      expect(result.exitCode, equals(0));
    }
  }

  // Runs a component and publishes the performance test results that
  // it produces, which the component should write to the file
  // componentOutputPath.
  Future<void> runTestComponent(
      {@required String packageName,
      @required String componentName,
      @required String commandArgs}) async {
    // Make a name for the realm that is very likely to be unique.
    // This allows us to clean up the output directory tree.
    final timestamp = DateTime.now().microsecondsSinceEpoch;
    final String realmName = 'perftest_$timestamp';

    const resultsFile = 'results.fuchsiaperf.json';
    final String targetOutputPath = '/tmp/r/sys/r/$realmName/'
        'fuchsia.com:$packageName:0#meta:$componentName/$resultsFile';
    final String command = 'run-test-component --realm-label=$realmName'
        ' fuchsia-pkg://fuchsia.com/$packageName#meta/$componentName'
        ' -- $commandArgs';

    final result = await sl4fDriver.ssh.run(command);
    expect(result.exitCode, equals(0));
    try {
      final File localResultsFile = await storage.dumpFile(
          targetOutputPath, 'results', 'fuchsiaperf_full.json');
      await processResultsSummarized([localResultsFile]);
    } finally {
      // Clean up: remove the output tree.
      final result = await sl4fDriver.ssh.run('rm -r /tmp/r/sys/r/$realmName');
      expect(result.exitCode, equals(0));
    }
  }
}

final _log = Logger('TouchInputLatencyMetricsProcessor');

// Custom MetricsProcessor for input latency tests that rely on trace events
// tagged with the "touch-input-test" category.
List<TestCaseResults> touchInputLatencyMetricsProcessor(
    Model model, Map<String, dynamic> extraArgs) {
  final inputLatency = getArgValuesFromEvents<num>(
          filterEventsTyped<InstantEvent>(getAllEvents(model),
              category: 'touch-input-test', name: 'input_latency'),
          'elapsed_time')
      .map((t) => t.toDouble())
      .toList();

  if (inputLatency.length != 1) {
    throw ArgumentError("touch-input-test didn't log an elapsed time.");
  }

  _log.info('Elapsed time: ${inputLatency.first} ns.');

  final List<TestCaseResults> testCaseResults = [
    TestCaseResults('touch_input_latency', Unit.nanoseconds, inputLatency)
  ];

  return testCaseResults;
}
