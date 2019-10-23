// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:logging/logging.dart';
import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:test/test.dart';

void main() {
  // Enable logging output.
  Logger.root
    ..level = Level.ALL
    ..onRecord.listen((rec) => print('[${rec.level}]: ${rec.message}'));

  sl4f.Sl4f sl4fDriver;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
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

    // TODO(fxb/23091): Process the results using catapult_converter.
  }, timeout: Timeout.none);
}
