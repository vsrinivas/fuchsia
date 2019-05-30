// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;

const _timeout = Duration(seconds: 60);

void main() {
  sl4f.Sl4f sl4fDriver;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  test('file facade writes to path', () async {
    String timestampName = (DateTime.now().millisecondsSinceEpoch.toString());
    final result = await sl4fDriver.request('file_facade.WriteFile',
        {'data': 'aDNsbDAgdzNyMWQh', 'dst': '/tmp/$timestampName'});
    if (result != 'Success') {
      fail('Call to write file unsuccessful, unable to write to device');
    }
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
      // This is a large test that waits for the DUT to come up and to start
      // rendering something.
      timeout: Timeout(_timeout));
}
