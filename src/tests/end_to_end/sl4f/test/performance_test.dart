// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:convert';
import 'dart:io';

import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:test/test.dart';

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
    test('trace via facade', () async {
      final traceSession = await performance.initializeTracing();
      await traceSession.start();
      await Future.delayed(Duration(seconds: 2));
      await traceSession.stop();
      final traceFile = await traceSession.terminateAndDownload('test-trace');

      expect(traceFile.path, matches(RegExp(r'-test-trace-trace.fxt$')));
      expect(
          dumpDir.listSync().map((f) => f.path.split('/').last),
          unorderedMatches([
            matches(RegExp(r'-test-trace-trace.fxt$')),
          ]));
    });

    test('get trace data without writing to file', () async {
      final traceSession = await performance.initializeTracing();
      await traceSession.start();
      await Future.delayed(Duration(seconds: 2));
      await traceSession.stop();
      final traceData = await traceSession.terminateAndDownloadAsBytes();
      expect(traceData, isNotEmpty);
      final jsonTraceData =
          await performance.convertTraceDataToJson(_trace2jsonPath, traceData);
      // Checking for the { at the end ensures that at least one trace event was
      // read from the collected trace data.
      expect(jsonTraceData,
          startsWith('{"displayTimeUnit":"ns","traceEvents":[{'));
    });

    test('get trace containing system metrics data', () async {
      await performance.stopSystemMetricsLogging();
      final traceSession = await performance
          .initializeTracing(categories: ['system_metrics_logger']);
      await performance.startSystemMetricsLogging(
          interval: Duration(milliseconds: 100),
          duration: Duration(minutes: 1));
      await traceSession.start();
      await Future.delayed(Duration(seconds: 2));
      await traceSession.stop();
      await performance.stopSystemMetricsLogging();
      final traceData = await traceSession.terminateAndDownloadAsBytes();
      expect(traceData, isNotEmpty);
      final jsonTraceData =
          await performance.convertTraceDataToJson(_trace2jsonPath, traceData);
      expect(jsonTraceData, contains('system_metrics_logger'));
    });
  }, timeout: Timeout(_timeout));
}

Future<List<String>> listDir(sl4f.Sl4f sl4f, String dir) async {
  final process = await sl4f.ssh.start('ls $dir');
  if (await process.exitCode != 0) {
    fail('unable to run ls under $dir');
  }
  final findResult = await process.stdout.transform(utf8.decoder).join();
  return findResult.split('\n');
}
