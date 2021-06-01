// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:inspect_dart_codelab_part_2_lib/reverser.dart';
import 'package:test/test.dart';

void main() {
  ReverserImpl openReverser() {
    return ReverserImpl(ReverserStats.noop());
  }

  test('reverser', () async {
    final reverser = openReverser();
    final result = await reverser.reverse('hello');
    expect(result, equals('olleh'));
  });
}
