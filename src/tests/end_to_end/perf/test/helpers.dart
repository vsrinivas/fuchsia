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
  sl4f.Component component;

  Future<void> setUp() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
    addTearDown(() async {
      await sl4fDriver.stopServer();
      sl4fDriver.close();
    });
    performance = sl4f.Performance(sl4fDriver);
    storage = sl4f.Storage(sl4fDriver);
    component = sl4f.Component(sl4fDriver);
  }

  static Future<PerfTestHelper> make() async {
    final helper = PerfTestHelper();
    await helper.setUp();
    return helper;
  }

  Future<void> processResults(String resultsFile) async {
    final localResultsFile =
        await storage.dumpFile(resultsFile, 'results', 'fuchsiaperf.json');

    await performance.convertResults('runtime_deps/catapult_converter',
        localResultsFile, Platform.environment);
  }
}
