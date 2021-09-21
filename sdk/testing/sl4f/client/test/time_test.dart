// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:mockito/mockito.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

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
  });
}
