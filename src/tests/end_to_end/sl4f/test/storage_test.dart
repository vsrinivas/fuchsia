// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:logging/logging.dart';
import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:test/test.dart';

const _timeout = Timeout(Duration(minutes: 1));

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.Storage storage;

  setUpAll(() async {
    Logger.root
      ..level = Level.ALL
      ..onRecord
          .listen((LogRecord rec) => print('[${rec.level}]: ${rec.message}'));

    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();

    storage = sl4f.Storage(sl4fDriver);
  });

  tearDownAll(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();

    Logger.root.clearListeners();
  });

  Future<bool> exists(String path) async {
    final result = await storage.stat(path);
    return result != null;
  }

  group('file facade', () {
    test('writes to path then reads from it', () async {
      String timestampName =
          'write-test-${DateTime.now().millisecondsSinceEpoch}';
      await storage.putBytes('/tmp/$timestampName', 'h3ll0 w0r1d!'.codeUnits);
      expect(await exists('/tmp/$timestampName'), true);

      final result = await storage.readFile('/tmp/$timestampName');
      expect(String.fromCharCodes(result), 'h3ll0 w0r1d!');
    });

    test('deletes file in path', () async {
      String timestampName =
          'delete-test-${DateTime.now().millisecondsSinceEpoch}';
      await storage.putBytes('/tmp/$timestampName', 'h3ll0 w3r1d!'.codeUnits);
      expect(await exists('/tmp/$timestampName'), true);

      String result = await storage.deleteFile('/tmp/$timestampName');
      expect(result, 'Success');

      expect(await exists('/tmp/$timestampName'), false);
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
      expect(await exists('/tmp/$timestampName'), true);
    });

    test('creates new dir recursively', () async {
      String timestampName =
          'mkdir-test-${DateTime.now().millisecondsSinceEpoch}';
      String subdir = 'subdir-test';
      String result = await storage.makeDirectory('/tmp/$timestampName/$subdir',
          recurse: true);
      expect(result, 'Success');

      expect(await exists('/tmp/$timestampName'), true);
      expect(await exists('/tmp/$timestampName/$subdir'), true);
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
  }, timeout: _timeout);
}
