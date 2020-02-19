// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io';

import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;

const _timeout = Duration(seconds: 60);
const String _trace2jsonPath = 'runtime_deps/trace2json';

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
  });

  tearDown(() async {
    dumpDir.deleteSync(recursive: true);

    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  group(sl4f.Sl4f, () {
    test('trace and download', () async {
      expect(
          await performance.trace(
              duration: Duration(seconds: 2), traceName: 'test-trace'),
          equals(true));

      await performance.downloadTraceFile('test-trace');

      expect(
          dumpDir.listSync().map((f) => f.path.split('/').last),
          unorderedMatches([
            matches(RegExp(r'-test-trace-trace.json$')),
          ]));
      expect(await listDir(sl4fDriver, '/tmp'),
          isNot(contains(matches(RegExp(r'test-trace-trace.json$')))));
    });

    test('download large trace', () async {
      // This obviously creates an invalid json file, but the act of downloading
      // said file shouldn't care about its contents.
      if ((await sl4fDriver.ssh.run(
                  'dd if=/dev/zero of=/tmp/fake-large-trace.json bs=1M count=40'))
              .exitCode !=
          0) {
        fail('Failed to create fake large trace file to download.');
      }

      await performance.downloadTraceFile('fake-large');

      expect(
          dumpDir.listSync().map((f) => f.path.split('/').last),
          unorderedMatches([
            matches(RegExp(r'-fake-large-trace.json$')),
          ]));

      final downloadedFile = dumpDir.listSync()[0];
      final stat = await downloadedFile.stat();

      expect(stat.size, equals(40 * 1024 * 1024));
    });

    test('binary trace, download, and convert', () async {
      expect(
          await performance.trace(
              duration: Duration(seconds: 2),
              traceName: 'test-binary-trace',
              binary: true),
          equals(true));

      final fxtFile = await performance.downloadTraceFile('test-binary-trace',
          binary: true);

      expect(
          dumpDir.listSync().map((f) => f.path.split('/').last),
          unorderedMatches([
            matches(RegExp(r'-test-binary-trace-trace.fxt$')),
          ]));

      await performance.convertTraceFileToJson(_trace2jsonPath, fxtFile);

      expect(
          dumpDir.listSync().map((f) => f.path.split('/').last),
          unorderedMatches([
            matches(RegExp(r'-test-binary-trace-trace.fxt$')),
            matches(RegExp(r'-test-binary-trace-trace.json$')),
          ]));
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
