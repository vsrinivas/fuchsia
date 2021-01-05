// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;

const _timeout = Duration(seconds: 60);
const _trace2jsonPath = 'runtime_deps/trace2json';

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.Dump dump;
  Directory dumpDir;
  sl4f.Performance performance;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
    dumpDir = await Directory.systemTemp.createTemp('temp-dump');
    dump = sl4f.Dump(dumpDir.path);

    performance = sl4f.Performance(sl4fDriver, dump);
    await performance.terminateExistingTraceSession();
  });

  tearDown(() async {
    await performance.terminateExistingTraceSession();
    dumpDir.deleteSync(recursive: true);

    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  group(sl4f.Sl4f, () {
    // Temperature data relies on the test environment having a valid config
    // with temperature sensors, which Astro has.
    test('get trace containing temperature data', () async {
      await performance.stopTemperatureLogging();
      final traceSession = await performance
          .initializeTracing(categories: ['temperature_logger']);
      await performance.startTemperatureLogging(
          interval: Duration(milliseconds: 100),
          duration: Duration(minutes: 1));
      await traceSession.start();
      await Future.delayed(Duration(seconds: 2));
      await traceSession.stop();
      await performance.stopTemperatureLogging();
      final traceData = await traceSession.terminateAndDownloadAsBytes();
      expect(traceData, isNotEmpty);
      final jsonTraceData =
          await performance.convertTraceDataToJson(_trace2jsonPath, traceData);
      expect(jsonTraceData, contains('temperature_logger'));
    });
  }, timeout: Timeout(_timeout));
}
