// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:test/test.dart';
import 'package:inspect_codelab_testing/integration_test.dart';
import 'package:fuchsia_logger/logger.dart';

void main() {
  setupLogger(
      name: 'inspect_dart_codelab', globalTags: ['part_5', 'integration_test']);

  test('start with fizzbuzz', () async {
    final env = await IntegrationTest.create(5);
    final reverser = env.connectToReverser();
    final result = await reverser.reverse('hello');
    expect(result, equals('olleh'));

    // [START result_hierarchy]
    final snapshot = await env.getReverserInspect();
    // [END result_hierarchy]
    expect(snapshot.length, 1);
    expect(
        snapshot[0].payload['root']['fuchsia.inspect.Health']['status'], 'OK');

    reverser.ctrl.close();
    env.dispose();
  });

  test('start without fizzbuzz', () async {
    final env = await IntegrationTest.create(5, includeFizzBuzz: false);
    final reverser = env.connectToReverser();
    final result = await reverser.reverse('hello');
    expect(result, equals('olleh'));

    final snapshot = await env.getReverserInspect();
    expect(snapshot.length, 1);
    final healthNode = snapshot[0].payload['root']['fuchsia.inspect.Health'];
    expect(healthNode['status'], 'UNHEALTHY');
    expect(healthNode['message'], 'FizzBuzz connection closed');

    reverser.ctrl.close();
    env.dispose();
  });
}
