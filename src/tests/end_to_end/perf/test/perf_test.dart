// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Helper code for setting up SL4F, running performance tests, and
// uploading the tests' results to the Catapult performance dashboard.

import 'dart:io' show Platform;

import 'package:logging/logging.dart';
import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:test/test.dart';

void enableLoggingOutput() {
  // This is necessary to get information about the commands the tests have
  // run, and to get information about what they outputted on stdout/stderr
  // if they fail.
  Logger.root
    ..level = Level.ALL
    ..onRecord.listen((rec) => print('[${rec.level}]: ${rec.message}'));
}

class PerfTestHelper {
  sl4f.Sl4f sl4fDriver;
  sl4f.Performance performance;
  sl4f.Storage storage;

  Future<void> setUp() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
    addTearDown(() async {
      await sl4fDriver.stopServer();
      sl4fDriver.close();
    });
    performance = sl4f.Performance(sl4fDriver);
    storage = sl4f.Storage(sl4fDriver);
  }

  static Future<PerfTestHelper> make() async {
    final helper = PerfTestHelper();
    await helper.setUp();
    return helper;
  }

  Future<void> processResults(String resultsFile) async {
    final localResultsFile =
        await storage.dumpFile(resultsFile, 'results', 'fuchsiaperf.json');

    // TODO(fxb/23091): Enable uploading to the Catapult dashboard after
    // this test has been removed from the old benchmarks runner (in
    // peridot/tests/benchmarks), and after we are able to test via trybots
    // what would get uploaded (TODO(fxb/39941)).
    await performance.convertResults('runtime_deps/catapult_converter',
        localResultsFile, Platform.environment,
        uploadToCatapultDashboard: false);
  }
}

void addTspecTest(String specFile) {
  test(specFile, () async {
    final helper = await PerfTestHelper.make();
    const resultsFile = '/tmp/perf_results.json';
    final result = await helper.sl4fDriver.ssh.run(
        'trace record --spec-file=$specFile --benchmark-results-file=$resultsFile');
    expect(result.exitCode, equals(0));
    await helper.processResults(resultsFile);
  }, timeout: Timeout.none);
}
