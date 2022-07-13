// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

// CODELAB: uncomment.
// import 'package:inspect_dart_codelab/reverser.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:test/test.dart';

void main() {
  setupLogger(
    name: 'inspect_dart_codelab',
    globalTags: ['part_1', 'unit_test'],
  );

  test('reverser', () async {
    // CODELAB: test the response from the reverser.
    // final reverser = ReverserImpl();
    // final result = await reverser.reverse('hello');
    // expect(result, equals('olleh'));
  });
}
