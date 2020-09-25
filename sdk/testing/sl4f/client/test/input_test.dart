// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';
import 'package:mockito/mockito.dart';
import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart';

class MockSl4f extends Mock implements Sl4f {}

void main(List<String> args) {
  MockSl4f sl4f;

  Duration duration = const Duration(milliseconds: 300);
  setUp(() {
    sl4f = MockSl4f();
  });

  group('without constructor-default rotation', () {
    Input input;
    setUp(() {
      input = Input(sl4f);
    });

    test('input rotates 0 degrees', () async {
      await input.swipe(Point<int>(0, 0), Point<int>(1000, 1000));
      verify(sl4f.request('input_facade.Swipe', {
        'x0': 0,
        'y0': 0,
        'x1': 1000,
        'y1': 1000,
        'duration': duration.inMilliseconds,
      }));
    });

    test('input rotates 90 degrees', () async {
      await input.swipe(Point<int>(0, 0), Point<int>(1000, 1000),
          screenRotation: Rotation.degrees90);
      verify(sl4f.request('input_facade.Swipe', {
        'x0': 1000,
        'y0': 0,
        'x1': 0,
        'y1': 1000,
        'duration': duration.inMilliseconds,
      }));
    });

    test('input rotates 180 degrees', () async {
      await input.tap(Point<int>(0, 0), screenRotation: Rotation.degrees180);
      verify(sl4f.request('input_facade.Tap', {
        'x': 1000,
        'y': 1000,
      })).called(1);
    });

    test('input rotates 270 degrees', () async {
      await input.tap(Point<int>(0, 0), screenRotation: Rotation.degrees270);
      verify(sl4f.request('input_facade.Tap', {
        'x': 0,
        'y': 1000,
      })).called(1);
    });

    test('input multiple taps', () async {
      await input.tap(Point<int>(500, 500), tapEventCount: 10, duration: 100);
      verify(sl4f.request('input_facade.Tap', {
        'x': 500,
        'y': 500,
        'tap_event_count': 10,
        'duration': 100,
      })).called(1);
    });

    test('input multi-finger taps', () async {
      final fingers = [
        Point(0, 0),
        Point(20, 20),
        Point(40, 40),
        Point(60, 60),
      ];

      await input.multiFingerTap(fingers, tapEventCount: 10, duration: 100);

      verify(sl4f.request('input_facade.MultiFingerTap', {
        'fingers': [
          {'finger_id': 1, 'x': 0, 'y': 0, 'width': 0, 'height': 0},
          {'finger_id': 2, 'x': 20, 'y': 20, 'width': 0, 'height': 0},
          {'finger_id': 3, 'x': 40, 'y': 40, 'width': 0, 'height': 0},
          {'finger_id': 4, 'x': 60, 'y': 60, 'width': 0, 'height': 0},
        ],
        'tap_event_count': 10,
        'duration': 100,
      })).called(1);
    });

    test('input multi-finger-swipe no-rotation', () async {
      await input.multiFingerSwipe([Point(0, 0), Point(10, 10), Point(20, 20)],
          [Point(500, 500), Point(750, 750), Point(1000, 1000)]);
      verify(sl4f.request('input_facade.multiFingerSwipe', {
        'fingers': [
          {'x0': 0, 'y0': 0, 'x1': 500, 'y1': 500},
          {'x0': 10, 'y0': 10, 'x1': 750, 'y1': 750},
          {'x0': 20, 'y0': 20, 'x1': 1000, 'y1': 1000}
        ],
        'duration': duration.inMilliseconds,
      }));
    });

    test('input multi-finger-swipe rotated-90', () async {
      await input.multiFingerSwipe([
        Point(0, 0),
        Point(125, 125),
        Point(250, 250)
      ], [
        Point(250, 250),
        Point(375, 375),
        Point(500, 500)
      ], screenRotation: Rotation.degrees90);
      verify(sl4f.request('input_facade.multiFingerSwipe', {
        'fingers': [
          {'x0': 1000, 'y0': 0, 'x1': 750, 'y1': 250},
          {'x0': 875, 'y0': 125, 'x1': 625, 'y1': 375},
          {'x0': 750, 'y0': 250, 'x1': 500, 'y1': 500}
        ],
        'duration': duration.inMilliseconds,
      }));
    });
  });

  test('input rotates with constructor default', () async {
    await Input(sl4f, Rotation.degrees270).tap(Point<int>(0, 0));
    verify(sl4f.request('input_facade.Tap', {
      'x': 0,
      'y': 1000,
    })).called(1);
  });
}
