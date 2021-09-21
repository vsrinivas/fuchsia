// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:fxutils/fxutils.dart';
import 'package:test/test.dart';

void main() {
  group('ListIterator', () {
    test('should return all values from `takeNext()`', () {
      final vals = <int>[1, 3, 2];
      final iter = ListIterator<int>.from(vals);
      expect(iter.takeNext(), 1);
      expect(iter.takeNext(), 3);
      expect(iter.takeNext(), 2);
    });

    test('should return lists from `take(n)`', () {
      final vals = <int>[1, 3, 2];
      final iter = ListIterator<int>.from(vals);
      expect(iter.take(2), [1, 3]);
      expect(iter.takeNext(), 2);
    });

    test('should correctly manage combinations of `take(n)` and `takeNext()`',
        () {
      final vals = <int>[1, 3, 2, 10, 9, 5, -1];
      final iter = ListIterator<int>.from(vals);
      expect(iter.takeNext(), 1);
      expect(iter.take(2), [3, 2]);
      expect(iter.take(3), [10, 9, 5]);
      // Returns as many as remain when you ask for too many
      expect(iter.take(3), [-1]);
    });

    test('should accurately report `hasMore()`', () {
      final vals = <int>[1, 3, 2];
      final iter = ListIterator<int>.from(vals);
      expect(iter.hasMore(), true);
      iter.take(2);
      expect(iter.hasMore(), true);
      iter.takeNext();
      expect(iter.hasMore(), false);
    });

    test('should accurately report `hasNMore(n)`', () {
      final vals = <int>[1, 3, 2];
      final iter = ListIterator<int>.from(vals);
      expect(iter.hasNMore(2), true);
      expect(iter.hasNMore(3), true);
      expect(iter.hasNMore(4), false);
      iter.take(2);
      expect(iter.hasNMore(1), true);
      expect(iter.hasNMore(2), false);
      iter.takeNext();
      expect(iter.hasNMore(1), false);
      expect(iter.hasNMore(2), false);
    });
  });
}
