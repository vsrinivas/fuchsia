// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';
import 'package:logging/logging.dart';
import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:sl4f/trace_processing.dart' as trace_processing;

final _log = Logger('encode-camera');
const _timeout = Duration(seconds: 60);
const List<String> _defaultCategories = [
  'memory_monitor',
  'media',
];
const String _catapultConverterPath = 'runtime_deps/catapult_converter';
const String _trace2jsonPath = 'runtime_deps/trace2json';

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.Performance performance;
  setUp(() async {
    Logger.root
      ..level = Level.ALL
      ..onRecord.listen((rec) => print('[${rec.level}]: ${rec.message}'));
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
    performance = sl4f.Performance(sl4fDriver);
    await performance.terminateExistingTraceSession();
  });
  tearDown(() async {
    await performance.terminateExistingTraceSession();
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });
  group(sl4f.Sl4f, () {
    const testCase = 'fuchsia.media.encode-camera';
    test(testCase, () async {
      const traceDuration = 10;
      final traceSession =
          await performance.initializeTracing(categories: _defaultCategories);

      await sl4f.Component(sl4fDriver).launch(
          'fuchsia-pkg://fuchsia.com/encode-camera#meta/encode_camera.cmx',
          ['--duration=20']);
      await Future.delayed(Duration(seconds: 5));
      await traceSession.start();
      await Future.delayed(Duration(seconds: traceDuration));
      await traceSession.stop();
      final traceFile = await traceSession.terminateAndDownload(testCase);
      expect(traceFile.path, matches(RegExp('-$testCase-trace.fxt\$')));
      final jsonTraceFile =
          await performance.convertTraceFileToJson(_trace2jsonPath, traceFile);

      _log.info('downloaded trace');
      final metricsSpecs = [sl4f.MetricsSpec(name: 'memory')];
      final metricsSpecSet =
          sl4f.MetricsSpecSet(testName: testCase, metricsSpecs: metricsSpecs);

      await performance.processTrace(metricsSpecSet, jsonTraceFile,
          converterPath: _catapultConverterPath);

      _log.info('processed trace');

      final traceModel =
          await trace_processing.createModelFromFilePath(jsonTraceFile.path);
      expect(traceModel, isNotNull);

      _processEncodeTrace(traceModel, traceDuration);
    });
  }, timeout: Timeout(_timeout));
}

void _processEncodeTrace(trace_processing.Model traceModel, int duration) {
  const String category = 'media';
  const String packetSentEventName = 'Media:PacketSent';
  // verify an appropriate number of frames were encoded
  // encode_camera selects a camera configuration that produces 30 fps
  const int frameRate = 30;

  int packetSentCount = 0;

  for (final p in traceModel.processes) {
    for (final t in p.threads) {
      for (final e in t.events) {
        if (e.category == category && e.name == packetSentEventName) {
          packetSentCount++;
        }
      }
    }
  }
  _log.info('encoder packets sent $packetSentCount');

  expect(packetSentCount, closeTo(duration * frameRate, 30));
}
