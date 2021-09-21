// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:test/test.dart';
import 'package:zircon/zircon.dart';

void main() {
  test('monotonic clock', () {
    final int time0 = System.clockGetMonotonic();
    final int time1 = System.clockGetMonotonic();
    expect(time1, greaterThan(time0));
  });
}
