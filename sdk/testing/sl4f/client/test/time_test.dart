// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:mockito/mockito.dart';
import 'package:test/test.dart';

import 'package:sl4f/sl4f.dart';

class MockSl4f extends Mock implements Sl4f {}

void main(List<String> args) {
  MockSl4f sl4f;
  Time time;

  setUp(() {
    sl4f = MockSl4f();
    time = Time(sl4f);
  });

  group(Time, () {
    test('System time', () async {
      final resultTime = DateTime.utc(2000, 1, 1);
      when(sl4f.request('time_facade.SystemTimeMillis'))
          .thenAnswer((_) async => resultTime.millisecondsSinceEpoch);

      expect(await time.systemTime(), equals(resultTime));
    });

    test('Kernel time', () async {
      final resultTime = DateTime.utc(2038, 1, 20);
      when(sl4f.request('time_facade.KernelTimeMillis'))
          .thenAnswer((_) async => resultTime.millisecondsSinceEpoch);

      expect(await time.kernelTime(), equals(resultTime));
    });
  });
}
