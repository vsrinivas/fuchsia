// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io';

import 'package:image/image.dart';
import 'package:logging/logging.dart';
import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:test/test.dart';

int _ignoreAlpha(int pixel) => pixel & 0x00ffffff;

/// Returns true if [image] is all black.
bool _isAllBlack(Image image) =>
    image.data.every((pixel) => _ignoreAlpha(pixel) == 0);

/// How many times to check the screen.
const _tries = 10;

/// How long to wait between screen checks.
const _delay = Duration(seconds: 10);

/// The path to the catapult converter binary.  It must exist in the package.
const _catapultConverterPath = 'runtime_deps/catapult_converter';

void main() {
  Logger.root
    ..level = Level.ALL
    ..onRecord.listen((rec) => print('[${rec.level}]: ${rec.message}'));
  sl4f.Sl4f sl4fDriver;
  sl4f.Scenic scenicDriver;

  final performance = sl4f.Performance(sl4fDriver);

  final log = Logger('screen_is_not_black_test');

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
    scenicDriver = sl4f.Scenic(sl4fDriver);
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  /// Exports the reboot timing results to a fuchsiaperf file.
  Future<void> exportTimings(String testSuite, Duration rebootDuration) async {
    final rebootResults = [
      // Host-side measurement of reboot duration.  Normally a measurement
      // fixture should not post-process results, but our test dashboard can't
      // deal with it at the moment.
      sl4f.TestCaseResults('Reboot', sl4f.Unit.milliseconds, [
        rebootDuration.inMilliseconds.toDouble(),
      ]),
    ];

    List<Map<String, dynamic>> results = [];
    for (final result in rebootResults) {
      results.add({
        'label': result.label,
        'test_suite': testSuite,
        'unit': sl4f.unitToCatapultConverterString(result.unit),
        'values': result.values,
        'split_first': result.splitFirst,
      });
    }

    File fuchsiaPerfFile = await sl4f.Dump().writeAsString(
        'screen_is_not_black_test', 'fuchsiaperf.json', json.encode(results));
    await performance.convertResults(
        _catapultConverterPath, fuchsiaPerfFile, Platform.environment);
    log.info('Catapult file created.');
  }

  test('the startup screen is not black', () async {
    // Reboot Fuchsia so that we are testing the initial startup state.
    //
    // This is necessary because other tests (currently
    // garnet_input_latency_benchmarks_test) kill Scenic, which would cause this
    // test to fail.  We use this as an opportunity to record the time taken
    // to reboot as a performance test result.
    var rebootDuration = await sl4fDriver.reboot();

    for (var attempt = 0; attempt < _tries; attempt++) {
      try {
        final screen = await scenicDriver.takeScreenshot(dumpName: 'screen');
        if (!_isAllBlack(screen)) {
          print('Saw a screen that is not black.');
          await exportTimings('fuchsia.boot', rebootDuration);
          return;
        }
      } on sl4f.JsonRpcException {
        print('Error taking screenshot; Scenic might not be ready yet.');
      }
      await Future.delayed(_delay);
    }
    fail('Screen was all black.');
  },
      // This is a large test that waits for the DUT to come up and to start
      // rendering something.
      timeout: Timeout.none);
}
