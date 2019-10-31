// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This runs some performance tests and converts their results for uploading to
// the Catapult performance dashboard.

import 'dart:io' show Platform;

import 'package:logging/logging.dart';
import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:test/test.dart';

void main() {
  // Enable logging output.
  Logger.root
    ..level = Level.ALL
    ..onRecord.listen((rec) => print('[${rec.level}]: ${rec.message}'));

  sl4f.Sl4f sl4fDriver;
  sl4f.Performance performance;
  sl4f.Storage storage;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
    performance = sl4f.Performance(sl4fDriver);
    storage = sl4f.Storage(sl4fDriver);
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  test('zircon_benchmarks', () async {
    const resultsFile = '/tmp/perf_results.json';
    final result = await sl4fDriver.ssh
        .run('/bin/zircon_benchmarks -p --out $resultsFile');
    expect(result.exitCode, equals(0));

    final localResultsFile =
        await storage.dumpFile(resultsFile, 'results', 'fuchsiaperf.json');

    // TODO(fxb/23091): Enable uploading to the Catapult dashboard after
    // this test has been removed from the old benchmarks runner (in
    // peridot/tests/benchmarks), and after we are able to test via trybots
    // what would get uploaded (TODO(fxb/39941)).
    await performance.convertResults('runtime_deps/catapult_converter',
        localResultsFile, Platform.environment,
        uploadToCatapultDashboard: false);
  }, timeout: Timeout.none);
}
