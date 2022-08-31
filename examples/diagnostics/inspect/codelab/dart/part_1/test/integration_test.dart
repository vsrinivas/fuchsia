// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:test/test.dart';
import 'package:fuchsia_logger/logger.dart';

void main() {
  setupLogger(
      name: 'inspect_dart_codelab', globalTags: ['part_1', 'integration_test']);

  test('start with fizzbuzz', () {
    // CODELAB: uncomment when it works.
    // final env = await IntegrationTest.create(1);
    // final reverser = env.connectToReverser();
    // final result = await reverser.reverse("hello");
    // expect(result, equals('olleh'));
    // reverser.ctrl.close();
    // env.dispose();
  });

  test('start without fizzbuzz', () {
    // CODELAB: uncomment when it works.
    // final env = await IntegrationTest.create(1, includeFizzBuzz: false);
    // final reverser = env.connectToReverser();
    // final result = await reverser.reverse("hello");
    // expect(result, equals('olleh'));
    // reverser.ctrl.close();
    // env.dispose();
  });
}
