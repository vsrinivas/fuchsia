// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:test/test.dart';

void main(List<String> arguments) {
  sl4f.Sl4f sl4fDriver;
  String psOutput;

  setUpAll(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
    // TODO(fxbug.dev/69468): Get this information from lifecycle streams instead.
    final result = await sl4fDriver.ssh.run('ps');
    psOutput = result.stdout;
  });

  tearDownAll(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  const services = [
    'log-stats.cm',
    'sampler.cm',
    'triage-detect.cm',
  ];

  group('diagnostics_running', () {
    for (final service in services) {
      test('Check $service running', () async {
        expect(psOutput, contains(service));
      });
    }
  });
}
