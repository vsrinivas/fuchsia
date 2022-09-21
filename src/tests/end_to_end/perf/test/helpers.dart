// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

// Helper code for setting up SL4F, running performance tests, and
// uploading the tests' results to the Catapult performance dashboard.

import 'dart:io' show File, Platform;

import 'package:args/args.dart';
import 'package:logging/logging.dart';
import 'package:meta/meta.dart';
import 'package:sl4f/sl4f.dart' as sl4f;
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
  static const String componentOutputPath =
      '/custom_artifacts/results.fuchsiaperf.json';

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
  Future<void> processResultsSummarized(List<File> jsonFiles,
      {String expectedMetricNamesFile}) async {
    final jsonSummaryData = summarizeFuchsiaPerfFiles(jsonFiles);

    final File jsonSummaryFile = dump.createFile('results', 'fuchsiaperf.json');
    await writeFuchsiaPerfJson(jsonSummaryFile, jsonSummaryData);

    await performance.convertResults('runtime_deps/catapult_converter',
        jsonSummaryFile, Platform.environment,
        expectedMetricNamesFile: expectedMetricNamesFile);
  }

  // Runs a command over SSH and publishes its output as performance
  // test results.
  //
  // The command to run is specified via a function that takes a
  // filename as an argument and returns a shell command string.  The
  // filename is for the results file that the command will write its
  // results to, in fuchsiaperf.json format.
  Future<void> runTestCommand(
      String Function(String resultsFilename) getCommand,
      {String expectedMetricNamesFile}) async {
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
      await processResultsSummarized([localResultsFile],
          expectedMetricNamesFile: expectedMetricNamesFile);
    } finally {
      // Clean up: remove the temporary file.
      final result = await sl4fDriver.ssh.run('rm -f $resultsFile');
      expect(result.exitCode, equals(0));
    }
  }

  // Runs the given component once and saves the performance results
  // (a fuchsiaperf.json file) in a host-side file, the path of which
  // is returned.  The argument resultsFileSuffix is included in the
  // name of that file.
  Future<File> runTestComponentReturningResultsFile(
      {@required String packageName,
      @required String componentName,
      @required String commandArgs,
      @required String resultsFileSuffix}) async {
    // Make a name for the output directory that is very likely to be
    // unique.
    final timestamp = DateTime.now().microsecondsSinceEpoch;
    final String targetOutputDir = '/tmp/perftest_$timestamp';

    final result = await sl4fDriver.ssh.run('mkdir $targetOutputDir');
    expect(result.exitCode, equals(0));

    try {
      final String command = 'run-test-suite'
          ' fuchsia-pkg://fuchsia.com/$packageName#meta/$componentName'
          ' --deprecated-output-directory $targetOutputDir -- $commandArgs';
      final result = await sl4fDriver.ssh.run(command);
      expect(result.exitCode, equals(0));

      // Search for the output file within the directory structure
      // produced by run-test-suite.
      final findResult = await sl4fDriver.ssh
          .run('find $targetOutputDir -name results.fuchsiaperf.json');
      final String findOutput = findResult.stdout.trim();
      expect(findOutput, isNot(equals('')));
      final List<String> targetOutputFiles = findOutput.split('\n');
      expect(targetOutputFiles.length, equals(1));

      return await storage.dumpFile(targetOutputFiles[0],
          'results_$packageName$resultsFileSuffix', 'fuchsiaperf_full.json');
    } finally {
      // Clean up: remove the output tree.
      final result = await sl4fDriver.ssh.run('rm -r $targetOutputDir');
      expect(result.exitCode, equals(0));
    }
  }

  // Runs a component and publishes the performance test results that
  // it produces, which the component should write to the file
  // componentOutputPath.
  Future<void> runTestComponent(
      {@required String packageName,
      @required String componentName,
      @required String commandArgs,
      String expectedMetricNamesFile,
      int processRuns = 1}) async {
    final List<File> localResultsFiles = [];
    for (var process = 0; process < processRuns; ++process) {
      localResultsFiles.add(await runTestComponentReturningResultsFile(
          packageName: packageName,
          componentName: componentName,
          commandArgs: commandArgs,
          resultsFileSuffix: '_process$process'));
    }
    await processResultsSummarized(localResultsFiles,
        expectedMetricNamesFile: expectedMetricNamesFile);
  }

  // Runs a component without processing any results.  This is useful when
  // the caller retrieves performance results via tracing.
  Future<void> runTestComponentWithNoResults(
      {@required String packageName,
      @required String componentName,
      @required String commandArgs}) async {
    final String command = 'run-test-suite'
        ' fuchsia-pkg://fuchsia.com/$packageName#meta/$componentName'
        ' -- $commandArgs';
    final result = await sl4fDriver.ssh.run(command);
    expect(result.exitCode, equals(0));
  }
}
