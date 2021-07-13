// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';
import 'package:zircon/zircon.dart';

void main() {
  test('monotonic clock', () {
    final int time0 = System.clockGetMonotonic();
    final int time1 = System.clockGetMonotonic();
    expect(time1, greaterThan(time0));
  });
}
