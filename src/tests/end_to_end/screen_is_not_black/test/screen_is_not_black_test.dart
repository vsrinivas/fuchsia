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

/// Collects uptime information.
class Uptime {
  // Elapsed time since last reboot.
  Duration sinceReboot;

  // Elapsed time from power-on until the time measurement was taken.  Could be
  // different than duration since reboot if the system monotonic clock is not
  // reset to zero.
  Duration sincePowerOn;

  // Low estimate for when reboot measurement was taken, from host side, wall clock.
  DateTime lowRebootWallClock;

  // High estimate for when reboot measurement was taken, from host side, wall clock.
  DateTime highRebootWallClock;

  Uptime(this.lowRebootWallClock, this.sinceReboot, this.highRebootWallClock,
      this.sincePowerOn);

  /// Returns an estimate of how long it was from Unix epoch to reboot.
  Duration sinceEpoch() {
    final Duration diff = highRebootWallClock.difference(lowRebootWallClock);
    final Duration halfDiff = Duration(microseconds: diff.inMicroseconds ~/ 2);
    final DateTime average = lowRebootWallClock.add(halfDiff);
    final rebootWallClock =
        average.difference(DateTime.fromMillisecondsSinceEpoch(0));
    return rebootWallClock - sinceReboot;
  }

  /// Elapsed time from power on until last reboot.
  Duration get rebootSincePowerOn => sincePowerOn - sinceReboot;
}

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

  // Finds the duration passed between the instant system timer showed zero and
  // the instant timekeeper was started.
  //
  // Uptime is tricky.  The system's uptime as reported by the monotonic clock
  // apparently doesn't always get reset to zero on a reboot.  So we may get a
  // monotonic clock reading that is showing a reading based on a zero reference
  // point unrelated to a recent reboot.
  //
  // Here's a proposed pragmatic way out.  Seeing as timekeeper is started
  // fairly early in a device's lifetime, we try to detect how long ago
  // timekeeper was started with reference to the zero point of the monotonic
  // clock.  If this time was too far in the past, assume that the duration
  // timer was not reset.  If this was within ~1 minute of zero reference
  // point, assume that the clock was indeed starting at zero.  Otherwise adopt
  // an arbitrary reference point which places startup of timekeeper.cmx about
  // 15 seconds after reboot.
  //
  // While this makes relative time measurements meaningless, we can still
  // compare successive runs and measure absolute time changes.
  Future<Uptime> findRebootTimestamp(
      sl4f.Inspect inspect, dynamic diagnostics) async {
    for (final item in diagnostics) {
      if (!item.toString().contains('timekeeper.cmx')) {
        continue;
      }
      final lowRebootWallClock = DateTime.now();
      final inspectResult = await inspect.inspectRecursively([item]);
      final highRebootWallClock = DateTime.now();

      final healthRoot = inspectResult?.single['contents']['root'];
      final uptimeNanos =
          healthRoot['current']['system_uptime_monotonic_nanos'];
      final uptime = Duration(microseconds: uptimeNanos ~/ 1e3);
      var sinceReboot = uptime;
      if (sinceReboot > Duration(minutes: 1)) {
        // If the system monotonic clock measures duration since power-on,
        // instead of duration since reboot, compute the adjusted uptime.
        final start = Duration(
            microseconds: healthRoot['start_time_monotonic_nanos'] ~/ 1e3);
        sinceReboot = sinceReboot - start + Duration(seconds: 15);
      }
      return Uptime(
          lowRebootWallClock, sinceReboot, highRebootWallClock, uptime);
    }

    log.info('System uptime was not found, using zero values instead.');
    return Uptime(DateTime.now(), Duration.zero, DateTime.now(), Duration.zero);
  }

  List<sl4f.TestCaseResults> record(String programName, String nodeName,
      dynamic root, String metricName, String renamedMetric, Duration reboot) {
    final node = root[nodeName];
    if (node == null) {
      return [];
    }
    final metricValue = (node[metricName] ?? 0) ~/ 1e3;
    final duration = Duration(microseconds: metricValue);
    List<sl4f.TestCaseResults> results = [
      sl4f.TestCaseResults(
          '$renamedMetric/$programName', sl4f.Unit.milliseconds, [
        duration.inMilliseconds.toDouble() - reboot.inMilliseconds.toDouble()
      ])
    ];
    return results;
  }

  // Records all results under 'node'.  The content of 'node' is assumed to
  // consist only of timestamps.
  List<sl4f.TestCaseResults> recordAll(String programName, String nodeName,
      dynamic root, String renamedMetrics, Duration reboot) {
    final node = root[nodeName];
    if (node == null) {
      return [];
    }
    List<sl4f.TestCaseResults> result = [];
    node.forEach((metric, measuredNanos) {
      final Duration vd = Duration(microseconds: measuredNanos ~/ 1e3);
      final Duration record = vd - reboot;
      result.add(sl4f.TestCaseResults('$renamedMetrics/$programName/$metric',
          sl4f.Unit.milliseconds, [record.inMilliseconds.toDouble()]));
    });
    return result;
  }

  /// Exports the reboot timing results to a fuchsiaperf file.  Requires that
  /// the `catapult_converter` binary is added to the runtime_deps, see the
  /// BUILD.gn file in this directory for the mechanics.
  Future<void> exportTimings(String testSuite, Duration rebootDuration) async {
    var rebootResults = [
      // The host-side measurement of reboot duration.
      sl4f.TestCaseResults('Reboot', sl4f.Unit.milliseconds,
          [rebootDuration.inMilliseconds.toDouble()]),
    ];

    var inspect = sl4f.Inspect(sl4fDriver.ssh);

    final diagnostics =
        (await inspect.retrieveHubEntries(filter: 'diagnostics')) ?? [];

    final Uptime uptime = await findRebootTimestamp(inspect, diagnostics);

    // Shorten program names like:
    // /hub/c/program.cmx/1245/out/diagnostics -> program.cmx
    // /hub/c/program.cmx/1245/out/diagnostics/root.inspect -> program.cmx
    final componentRe =
        RegExp(r'/c/([^/]*)/[0-9]+/out/diagnostics(/root.inspect)*');
    // Collect all health nodes and other metrics relevant for the boot process
    // from the running programs.  Some programs, like 'timekeeper' and `kcounter_inspect`
    // are more interesting since they have metrics useful for analyzing the timing of the
    // boot process.
    for (final item in diagnostics) {
      final label = componentRe.firstMatch(item)?.group(1) ?? item;
      final inspectResult = await inspect.inspectRecursively([item]);
      final rootNode = inspectResult?.single['contents']['root'];
      if (rootNode == null) {
        continue;
      }

      // For all nodes that have it, add health node information.
      rebootResults.addAll(record(
              label,
              'fuchsia.inspect.Health',
              rootNode,
              'start_timestamp_nanos',
              'TimeToStart',
              uptime.rebootSincePowerOn) +
          // Record all durations under the node named 'fuchsia.inspect.Timestamps'.
          recordAll(label, 'fuchsia.inspect.Timestamps', rootNode, 'Durations',
              uptime.sinceEpoch()));
    }

    List<Map<String, dynamic>> results = [];
    for (final result in rebootResults) {
      results.add(result.toJson(testSuite: testSuite));
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
