// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//import 'dart:io';

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

  group(sl4f.Sl4f, () {
    test('ComponentSearch search component not running', () async {
      final result = await sl4f.ComponentSearch(sl4fDriver).search('fake.cmx');
      expect(result, false);
    });

    test('ComponentSearch search a running component', () async {
      final result = await sl4f.ComponentSearch(sl4fDriver).search('sl4f.cmx');
      expect(result, true);
    });

    test('ComponentSearch List running components', () async {
      final result = await sl4f.ComponentSearch(sl4fDriver).list();
      expect(result.isNotEmpty, true);
    });
  }, timeout: Timeout(_timeout));
}
