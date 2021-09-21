// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:logging/logging.dart';
import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:sl4f/trace_processing.dart' as trace_processing;
import 'package:test/test.dart';

final _log = Logger('bt-a2dp-e2e-test');
const _timeout = Duration(seconds: 60);
const List<String> _defaultCategories = [
  'bluetooth',
  'bt-a2dp',
  'stream-sink',
];
const String _trace2jsonPath = 'runtime_deps/trace2json';

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.Performance performance;
  sl4f.Modular basemgrController;

  setUp(() async {
    Logger.root
      ..level = Level.ALL
      ..onRecord.listen((rec) => print('[${rec.level}]: ${rec.message}'));
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
    performance = sl4f.Performance(sl4fDriver);
    basemgrController = sl4f.Modular(sl4fDriver);
    await performance.terminateExistingTraceSession();
  });
  tearDown(() async {
    await performance.terminateExistingTraceSession();
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });
  group(sl4f.Sl4f, () {
    const testCase = 'fuchsia.bluetooth.a2dp';
    test(testCase, () async {
      await basemgrController.boot();
      final traceSession =
          await performance.initializeTracing(categories: _defaultCategories);
      const testDurationSeconds = 10;

      await traceSession.start();

      expect(
          await sl4f.Component(sl4fDriver).launch(
              'fuchsia-pkg://fuchsia.com/bt-a2dp-loopback#meta/bt-a2dp-loopback.cmx',
              ['--duration', testDurationSeconds.toString()]),
          'Success');

      await traceSession.stop();
      final traceFile = await traceSession.terminateAndDownload(testCase);
      expect(traceFile.path, matches(RegExp('-$testCase-trace.fxt\$')));
      final jsonTraceFile =
          await performance.convertTraceFileToJson(_trace2jsonPath, traceFile);

      _log.info('downloaded trace');

      final traceModel =
          await trace_processing.createModelFromFilePath(jsonTraceFile.path);
      expect(traceModel, isNotNull);

      _processA2DPTrace(traceModel, testDurationSeconds);
      await basemgrController.shutdown(forceShutdownBasemgr: false);
    });
  }, timeout: Timeout(_timeout));
}

void _processA2DPTrace(
    trace_processing.Model traceModel, int testDurationSeconds) {
  const String a2dpCategory = 'bt-a2dp';
  const String packetSentEventName = 'Media:PacketSent';
  // Add plenty extra initiator delay since there is delay in getting components
  // launched when running this test in CQ.
  const initiatorDelayMilliseconds = 500 + 5000;

  // 24ms chunks of data from source. This will need to be changed if the
  // negotiated MTU changes such that source is sending different amounts per
  // packet.
  final expectedPackets =
      (((testDurationSeconds * 1000) - initiatorDelayMilliseconds) / 24)
          .floor();
  int packetSentCount = 0;

  for (final p in traceModel.processes) {
    for (final t in p.threads) {
      for (final e in t.events) {
        if (e.name == packetSentEventName && e.category == a2dpCategory) {
          packetSentCount++;
        }
      }
    }
  }
  _log.info('packets sent $packetSentCount expectedPackets $expectedPackets');

  // TODO(67888) Fix flake when checking for exact expected packets running
  // in CQ environment.
  expect(packetSentCount, greaterThan(0));
}
