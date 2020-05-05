// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;

const _timeout = Duration(seconds: 60);

Future<void> _validateNode(sl4f.Inspect inspect, String selector) async {
  final list = selector.split(':');
  if (list.length != 3) {
    fail('selector format should contain 2 colons');
  }

  final top = await inspect.snapshot(['$selector']);
  if (top == null || top.isEmpty) {
    print('inspect selector $selector does not exist');    
    return;
  }

  final paths = list[1].split('/');
  var node = top[0]['payload'];
  for (final name in paths) {
    node = node[name];
  }

  final result = node[list[2]];
  if (result == null) {
    print('inspect property ${list[2]} does not exist for selector $selector');
    return;
  }
}

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

  test('inspect metrics tests inspection nodes written by metrics', () async {
    // The test will read each of the node for disk usages
    // fx set ... --release --with=//src/tests/end_to_end/inspect_metrics:test --with garnet/bin/sl4f:bin
    // fx build
    // fx run-e2e-tests inspect_metrics_test # which sets FUCHSIA_IPV4_ADDR automatically

    final inspect = sl4f.Inspect(sl4fDriver);

    // The following paths are used by reader code in b/152262213. Please notify if the paths are moved.
    // There should be plan on how to move the path for the reader side to work before and after the move.
    // Refer to design doc in go/fuchsia-metrics-to-inspect-design.
    await _validateNode(
        inspect,
        'archivist.cmx:root/data_stats/global_data/global_data/cache:size');
    await _validateNode(
        inspect,
        'archivist.cmx:root/data_stats/global_data/stats:used_bytes');
  },
      // This is a large test that waits for the DUT to come up.
      timeout: Timeout(_timeout));
}
