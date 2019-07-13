// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';
import 'dart:math';

import 'package:mockito/mockito.dart';
import 'package:test/test.dart';

import 'package:sl4f/sl4f.dart';

class MockSsh extends Mock implements Ssh {}

void main(List<String> args) {
  Ssh ssh;
  setUp(() {
    ssh = MockSsh();
    when(ssh.run(any))
        .thenAnswer((_) => Future.value(ProcessResult(0, 0, null, null)));
  });

  group('without constructor default rotation', () {
    Input input;
    setUp(() {
      input = Input(Sl4f('', ssh));
    });

    test('input rotates 0 degrees', () async {
      final result =
          await input.swipe(Point<int>(0, 0), Point<int>(1000, 1000));
      verify(ssh.run('input --duration=300 swipe 0 0 1000 1000'));
      expect(result, true);
    });

    test('input rotates 90 degrees', () async {
      final result = await input.swipe(Point<int>(0, 0), Point<int>(1000, 1000),
          screenRotation: Rotation.degrees90);
      verify(ssh.run('input --duration=300 swipe 1000 0 0 1000'));
      expect(result, true);
    });

    test('input rotates 180 degrees', () async {
      final result = await input.tap(Point<int>(0, 0),
          screenRotation: Rotation.degrees180);
      verify(ssh.run('input tap 1000 1000'));
      expect(result, true);
    });

    test('input rotates 270 degrees', () async {
      final result = await input.tap(Point<int>(0, 0),
          screenRotation: Rotation.degrees270);
      verify(ssh.run('input tap 0 1000'));
      expect(result, true);
    });
  });

  test('input rotates with constructor default', () async {
    final result =
        await Input(Sl4f('', ssh), Rotation.degrees270).tap(Point<int>(0, 0));
    verify(ssh.run('input tap 0 1000'));
    expect(result, true);
  });
}
