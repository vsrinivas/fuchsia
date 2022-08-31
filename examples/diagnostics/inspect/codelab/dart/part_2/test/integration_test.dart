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
      name: 'inspect_dart_codelab', globalTags: ['part_2', 'integration_test']);

  test('start with fizzbuzz', () async {
    final env = await IntegrationTest.create(2);
    final reverser = env.connectToReverser();
    final result = await reverser.reverse('hello');
    expect(result, equals('olleh'));
    // CODELAB: Check that the component was connected to FizzBuzz.
    reverser.ctrl.close();
    env.dispose();
  });

  test('start without fizzbuzz', () async {
    final env = await IntegrationTest.create(2, includeFizzBuzz: false);
    final reverser = env.connectToReverser();
    final result = await reverser.reverse('hello');
    expect(result, equals('olleh'));
    // CODELAB: Check that the component failed to connect to FizzBuzz.
    reverser.ctrl.close();
    env.dispose();
  });
}
