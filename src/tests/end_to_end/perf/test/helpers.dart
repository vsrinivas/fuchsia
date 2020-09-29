// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Helper code for setting up SL4F, running performance tests, and
// uploading the tests' results to the Catapult performance dashboard.

import 'dart:io' show Platform;

import 'package:args/args.dart';
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
