// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';

import 'package:mockito/mockito.dart';
import 'package:test/test.dart';

import 'package:sl4f/sl4f.dart';

class MockSl4f extends Mock implements Sl4f {}

void main(List<String> args) {
  Sl4f sl4f;
  setUp(() {
    sl4f = MockSl4f();
    when(sl4f.ssh(any)).thenAnswer((_) => Future.value(true));
  });

  test('input rotates 0 degrees', () async {
    final result =
        await Input(sl4f).swipe(Point<int>(0, 0), Point<int>(1000, 1000));
    verify(sl4f.ssh('input --duration=300 swipe 0 0 1000 1000'));
    expect(result, true);
  });

  test('input rotates 90 degrees', () async {
    final result = await Input(sl4f).swipe(
        Point<int>(0, 0), Point<int>(1000, 1000),
        screenRotation: Rotation.degrees90);
    verify(sl4f.ssh('input --duration=300 swipe 1000 0 0 1000'));
    expect(result, true);
  });

  test('input rotates 180 degrees', () async {
    final result = await Input(sl4f)
        .tap(Point<int>(0, 0), screenRotation: Rotation.degrees180);
    verify(sl4f.ssh('input tap 1000 1000'));
    expect(result, true);
  });

  test('input rotates 270 degrees', () async {
    final result = await Input(sl4f)
        .tap(Point<int>(0, 0), screenRotation: Rotation.degrees270);
    verify(sl4f.ssh('input tap 0 1000'));
    expect(result, true);
  });

  test('input rotates with constructor default', () async {
    final result = await Input(sl4f, Rotation.degrees270).tap(Point<int>(0, 0));
    verify(sl4f.ssh('input tap 0 1000'));
    expect(result, true);
  });
}
