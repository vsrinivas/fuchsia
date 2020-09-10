// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:path/path.dart' as p;
import 'package:test/test.dart';
import 'package:logging/logging.dart';
import 'package:sl4f/sl4f.dart' as sl4f;

const _timeout = Timeout(Duration(minutes: 1));

void printErrorHelp() {
  print('If this test fails, see '
      'https://fuchsia.googlesource.com/a/fuchsia/+/master/src/tests/end_to_end/copy_tests/README.md'
      ' for details!');
}

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.Storage storage;
  Directory tempDir;

  setUpAll(() async {
    Logger.root
      ..level = Level.ALL
      ..onRecord.listen((rec) => print('[${rec.level}]: ${rec.message}'));

    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
    storage = sl4f.Storage(sl4fDriver);
    tempDir = await Directory.systemTemp.createTemp('test-dir');
  });

  tearDownAll(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
    Logger.root.clearListeners();

    printErrorHelp();
  });

  List<String> makeCopyToArgs(String source, String destination) =>
      sl4fDriver.ssh.defaultArguments +
      [source, '[${sl4fDriver.ssh.target}]:$destination'];

  List<String> makeCopyFromArgs(String source, String destination) =>
      sl4fDriver.ssh.defaultArguments +
      ['[${sl4fDriver.ssh.target}]:$source', destination];

  ProcessResult runCopyProcess(List<String> args) =>
      Process.runSync('scp', args,
          // If not run in a shell it doesn't seem like the PATH is searched.
          runInShell: true);

  group('Copy files', () {
    test('copy from device', () async {
      String timestampName =
          'copy-from-device-test-${DateTime.now().millisecondsSinceEpoch}';
      await storage.putBytes('/tmp/$timestampName', 'h3ll0 w0r1d!'.codeUnits);

      final result =
          runCopyProcess(makeCopyFromArgs('/tmp/$timestampName', tempDir.path));
      expect(result.exitCode, equals(0));

      String data =
          await File(p.join(tempDir.path, '$timestampName')).readAsString();
      expect(data, equals('h3ll0 w0r1d!'));
    });

    test('copy to device', () async {
      String timestampName =
          'copy-to-device-test-${DateTime.now().millisecondsSinceEpoch}';
      String filePath = p.join(tempDir.path, '$timestampName');
      await File(filePath).writeAsString('!d1r3w 0ll3h');

      final result = runCopyProcess(makeCopyToArgs(filePath, '/tmp/'));
      expect(result.exitCode, equals(0));

      List<int> data =
          await sl4f.Storage(sl4fDriver).readFile('/tmp/$timestampName');
      expect(String.fromCharCodes(data), '!d1r3w 0ll3h');
    });
  }, timeout: _timeout);
}
