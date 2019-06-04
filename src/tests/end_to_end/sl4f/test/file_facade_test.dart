// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;

const _timeout = Duration(seconds: 60);

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.Storage storage;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();

    storage = sl4f.Storage(sl4fDriver);
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  test('file facade writes to path', () async {
    String timestampName = (DateTime.now().millisecondsSinceEpoch.toString());
    await storage.putBytes('/tmp/$timestampName', 'h3ll0 w3r1d!'.codeUnits);
    final process = await sl4fDriver.sshProcess('ls /tmp');
    if (await process.exitCode != 0) {
      fail('unable to find files under the folder');
    }
    final stringFindResult =
        await process.stdout.transform(utf8.decoder).join();
    if (stringFindResult.split('\n').toSet().contains(timestampName)) {
      print('Succesfully verified the file /tmp/$timestampName was created.');
      return;
    }
    fail('Unable to verify that the file was created.');
  },
      // This is a large test that waits for the DUT to come up.
      timeout: Timeout(_timeout));
}
