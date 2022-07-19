// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:io';

import 'package:logging/logging.dart';
import 'package:path/path.dart' as p;
import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:test/test.dart';

const _timeout = Timeout(Duration(minutes: 1));

void printErrorHelp() {
  print('If this test fails, see '
      'https://fuchsia.googlesource.com/a/fuchsia/+/HEAD/src/tests/end_to_end/copy_files/README.md'
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

  List<String> makeCopyToArgs(String source, String destination) {
    List<String> args = sl4fDriver.ssh.defaultArguments +
        [source, '[${sl4fDriver.ssh.target}]:$destination'];
    // scp uses `-P` instead of `-p` to set the port number.
    int index = args.indexOf('-p');
    args.replaceRange(index, index + 1, ['-P']);
    return args;
  }

  List<String> makeCopyFromArgs(String source, String destination) {
    List<String> args = sl4fDriver.ssh.defaultArguments +
        ['[${sl4fDriver.ssh.target}]:$source', destination];
    // scp uses `-P` instead of `-p` to set the port number.
    int index = args.indexOf('-p');
    args.replaceRange(index, index + 1, ['-P']);
    return args;
  }

  void runCopyProcess(List<String> args) {
    final result = Process.runSync('scp', args,
        // If not run in a shell it doesn't seem like the PATH is searched.
        runInShell: true);
    expect(result.exitCode, equals(0),
        reason:
            '`scp ${args.join(' ')}` returned a non-zero integer: ${result.exitCode}.\nstdout of scp: "${result.stdout}"\nstderr of scp: "${result.stderr}"');
  }

  group('Copy files', () {
    test('copy from device', () async {
      String timestampName =
          'copy-from-device-test-${DateTime.now().millisecondsSinceEpoch}';
      await storage.putBytes('/tmp/$timestampName', 'h3ll0 w0r1d!'.codeUnits);

      runCopyProcess(makeCopyFromArgs('/tmp/$timestampName', tempDir.path));
      String data =
          await File(p.join(tempDir.path, '$timestampName')).readAsString();
      expect(data, equals('h3ll0 w0r1d!'));
    });

    test('copy to device', () async {
      String timestampName =
          'copy-to-device-test-${DateTime.now().millisecondsSinceEpoch}';
      String filePath = p.join(tempDir.path, '$timestampName');
      await File(filePath).writeAsString('!d1r3w 0ll3h');

      runCopyProcess(makeCopyToArgs(filePath, '/tmp/'));
      List<int> data =
          await sl4f.Storage(sl4fDriver).readFile('/tmp/$timestampName');
      expect(String.fromCharCodes(data), '!d1r3w 0ll3h');
    });
  }, timeout: _timeout);
}
