// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;

const _timeout = Timeout(Duration(minutes: 1));

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.Tiles tiles;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();

    tiles = sl4f.Tiles(sl4fDriver);
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  group(sl4f.Tiles, () {
    test('start tiles', () async {
      // If anything throws an exception then we've failed.
      await tiles.start();
    });
    test('stop tiles', () async {
      // If anything throws an exception then we've failed.
      await tiles.stop();
    });
    test('add list remove tiles', () async {
      final key = await tiles.addFromUrl(
          'fuchsia-pkg://fuchsia.com/spinning_square_view#meta/spinning_square_view.cmx',
          allowFocus: true);
      expect(key, isNotNull);

      final results = await tiles.list();
      expect(results, isNotNull);
      expect(results.length, equals(1));
      expect(results.first.key, equals(key));

      await tiles.remove(key);

      final results2 = await tiles.list();
      expect(results2, isNotNull);
      expect(results2.length, equals(0));
    });
  }, timeout: _timeout);
}
