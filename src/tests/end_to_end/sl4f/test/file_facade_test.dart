// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';

import 'package:logging/logging.dart';
import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:test/test.dart';

const _timeout = Duration(seconds: 60);

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.Storage storage;

  setUpAll(() async {
    Logger.root
        ..level = Level.ALL
        ..onRecord.listen(
            (LogRecord rec) => print('[${rec.level}]: ${rec.message}'));

    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();

    storage = sl4f.Storage(sl4fDriver);
  });

  tearDownAll(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();

    Logger.root.clearListeners();
  });

  group('file facade', () {
    test('writes to path', () async {
      String timestampName =
          'write-test-${DateTime.now().millisecondsSinceEpoch}';
      await storage.putBytes('/tmp/$timestampName', 'h3ll0 w3r1d!'.codeUnits);

      expect(await listDir(sl4fDriver, '/tmp'), contains(timestampName));
    });

    test('reads from path', () async {
      String timestampName =
          'read-test-${DateTime.now().millisecondsSinceEpoch}';
      if (!await sl4fDriver.ssh('echo -n "!d1r3w 0ll3h" > /tmp/$timestampName')) {
        fail('failed to create file to test read.');
      }

      final result = await storage.readFile('/tmp/$timestampName');
      expect(String.fromCharCodes(result), '!d1r3w 0ll3h');
    });

    test('deletes file in path', () async {
      String timestampName =
          'delete-test-${DateTime.now().millisecondsSinceEpoch}';
      if (!await sl4fDriver.ssh('echo "exists" > /tmp/$timestampName')) {
        fail('failed to create file to test delete.');
      }

      String result = await storage.deleteFile('/tmp/$timestampName');
      expect(result, 'Success');

      expect(await listDir(sl4fDriver, '/tmp'), isNot(contains(timestampName)));
    });

    test('returns notfound when deleting unexisting code', () async {
      String timestampName =
          'delete-notexist-test-${DateTime.now().millisecondsSinceEpoch}';
      String result = await storage.deleteFile('/tmp/$timestampName');
      expect(result, 'NotFound');
    });

    test('creates new dir', () async {
      String timestampName =
          'mkdir-test-${DateTime.now().millisecondsSinceEpoch}';
      String result = await storage.makeDirectory('/tmp/$timestampName');

      expect(result, 'Success');
      expect(await listDir(sl4fDriver, '/tmp'), contains(timestampName));
    });

    test('creates new dir recursively', () async {
      String timestampName =
          'mkdir-test-${DateTime.now().millisecondsSinceEpoch}';
      String subdir = 'subdir-test';
      String result = await storage.makeDirectory('/tmp/$timestampName/$subdir',
          recurse: true);
      expect(result, 'Success');

      expect(await listDir(sl4fDriver, '/tmp'), contains(timestampName));
      expect(
          await listDir(sl4fDriver, '/tmp/$timestampName'), contains(subdir));
    });

    test('returns alreadyexists when creating existing directory', () async {
      String timestampName =
          'mkdir-test-${DateTime.now().millisecondsSinceEpoch}';
      String subdir = 'subdir-test';

      String result = await storage.makeDirectory('/tmp/$timestampName');
      expect(result, 'Success');
      result = await storage.makeDirectory('/tmp/$timestampName/$subdir');
      expect(result, 'Success');

      result = await storage.makeDirectory('/tmp/$timestampName/$subdir',
          recurse: true);
      expect(result, 'AlreadyExists');
    });
  },
      // This is a large test that waits for the DUT to come up.
      timeout: Timeout(_timeout));
}

Future<List<String>> listDir(sl4f.Sl4f sl4f, String dir) async {
  final process = await sl4f.sshProcess('ls $dir');
  if (await process.exitCode != 0) {
    fail('unable to run ls under $dir');
  }
  final findResult = await process.stdout.transform(utf8.decoder).join();
  return findResult.split('\n');
}
