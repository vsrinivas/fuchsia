// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';
import 'package:test/test.dart';

void main() {
  group('Simulator', () {
    test('output-all', () async {
      // HACK(mesch): This command line makes assumptions about the current
      // working directory. The invariant way would be to use the
      // Platform.script URI. This works when running the test directly in dart.
      // However, when run from the modular test script, the Process.script URI
      // is a data: URI and cannot be used to resolve a relative file name. :(
      final ProcessResult result = await Process
          .run('../third_party/flutter/bin/cache/dart-sdk/bin/dart', [
        'bin/run.dart',
        '--exec=output-all',
        '../examples/recipes/reserve-restaurant.yaml'
      ]);
      expect(result.stderr, equals(''));
      expect(
          result.stdout,
          contains('- [8] table-reserve ::'
              ' [9] day -> meal -> table-reservation *'));
    });
  });
}
