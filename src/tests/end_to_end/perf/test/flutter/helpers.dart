// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:logging/logging.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

import '../helpers.dart';

// TODO(53934): Re-evaluate what code is shared between tests after adding
// more Flutter benchmarks. Possibly merge this helper with or make it extend
// PerfTestHelper.
class FlutterTestHelper {
  Sl4f sl4f;
  Logger log;

  Performance perf;
  Tiles tiles;

  // TODO(55832): Add flutter_driver support.

  /// The tile key added by [launchApp]; stored so the tile can be removed by
  /// [_tearDown].
  int _tileKey;

  /// Create a [FlutterTestHelper] with an active sl4f connection.
  ///
  /// Must be called from within a test; will add a teardown and will [fail] the
  /// test if an error occurs when setting up the helper.
  static Future<FlutterTestHelper> make() async {
    final helper = FlutterTestHelper();
    await helper.setUp();
    return helper;
  }

  Future<void> setUp() async {
    enableLoggingOutput();

    log = Logger('FlutterPerfTest');

    sl4f = Sl4f.fromEnvironment();
    await sl4f.startServer();
    if (sl4f == null) {
      fail('Failed to start sl4f.');
    } else {
      log.fine('Started sl4f.');
    }

    perf = Performance(sl4f);
    tiles = Tiles(sl4f);

    addTearDown(_tearDown);
  }

  Future<void> _tearDown() async {
    if (_tileKey != null) {
      await tiles.remove(_tileKey);
      log.fine('Removed tile with key $_tileKey');
    }

    await sl4f.stopServer();
    sl4f.close();
  }

  /// Launch a given app using tiles_ctl.
  Future<void> addTile(String appName) async {
    _tileKey = await tiles
        .addFromUrl('fuchsia-pkg://fuchsia.com/$appName#meta/$appName.cmx');
    log.info('Launched $appName with tile key $_tileKey.');
  }
}
