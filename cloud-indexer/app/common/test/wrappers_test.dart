// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:cloud_indexer_common/wrappers.dart';
import 'package:googleapis/storage/v1.dart' as storage_api;
import 'package:mockito/mockito.dart';
import 'package:test/test.dart';

class MockStorageApi extends Mock implements storage_api.StorageApi {}

class MockObjectsResourceApi extends Mock
    implements storage_api.ObjectsResourceApi {}

main() {
  group('ThresholdSink', () {
    const List<int> testBytes1 = const [1, 2, 3];
    const List<List<int>> testBytes2 = const [
      const [4, 5, 6],
      const [7, 8, 9]
    ];
    const List<int> testBytes3 = const [10, 11, 12];

    List<int> aboveBuffer;
    List<int> belowBuffer;

    setUp(() {
      aboveBuffer = new List<int>();
      belowBuffer = new List<int>();
    });

    tearDown(() {
      aboveBuffer = null;
      belowBuffer = null;
    });

    List<int> accumulate(List<int> buffer, List<int> bytes) =>
        buffer..addAll(bytes);

    List<int> allTestBytes() => <int>[]
      ..addAll(testBytes1)
      ..addAll(testBytes2.expand((i) => i))
      ..addAll(testBytes3);

    test('above, success', () async {
      StreamSink<List<int>> thresholdSink = new ThresholdSink(
          11,
          (Stream<List<int>> stream, _) => stream
              .fold(aboveBuffer, accumulate)
              .then((_) => 'above, success'),
          (Stream<List<int>> stream, _) =>
              stream.fold(belowBuffer, accumulate));
      thresholdSink.add(testBytes1);
      await thresholdSink.addStream(new Stream.fromIterable(testBytes2));
      thresholdSink.add(testBytes3);

      expect(await thresholdSink.close(), 'above, success');
      expect(aboveBuffer, allTestBytes());
      expect(belowBuffer, []);
    });

    test('above, error', () async {
      StreamSink<List<int>> thresholdSink = new ThresholdSink(
          11,
          (Stream<List<int>> stream, _) => stream
              .fold(aboveBuffer, accumulate)
              .then((_) => throw 'above, error'),
          (Stream<List<int>> stream, _) =>
              stream.fold(belowBuffer, accumulate));
      thresholdSink.add(testBytes1);
      await thresholdSink.addStream(new Stream.fromIterable(testBytes2));
      thresholdSink.add(testBytes3);

      try {
        await thresholdSink.close();
      } catch (e) {
        expect(e, 'above, error');
      }

      expect(aboveBuffer, allTestBytes());
      expect(belowBuffer, []);
    });

    test('below, success', () async {
      StreamSink<List<int>> thresholdSink = new ThresholdSink(
          12,
          (Stream<List<int>> stream, _) => stream.fold(aboveBuffer, accumulate),
          (Stream<List<int>> stream, _) => stream
              .fold(belowBuffer, accumulate)
              .then((_) => 'below, success'));
      thresholdSink.add(testBytes1);
      await thresholdSink.addStream(new Stream.fromIterable(testBytes2));
      thresholdSink.add(testBytes3);

      expect(await thresholdSink.close(), 'below, success');
      expect(aboveBuffer, []);
      expect(belowBuffer, allTestBytes());
    });

    test('below, error', () async {
      StreamSink<List<int>> thresholdSink = new ThresholdSink(
          12,
          (Stream<List<int>> stream, _) => stream.fold(aboveBuffer, accumulate),
          (Stream<List<int>> stream, _) => stream
              .fold(belowBuffer, accumulate)
              .then((_) => throw 'below, error'));
      thresholdSink.add(testBytes1);
      await thresholdSink.addStream(new Stream.fromIterable(testBytes2));
      thresholdSink.add(testBytes3);

      try {
        await thresholdSink.close();
      } catch (e) {
        expect(e, 'below, error');
      }

      expect(aboveBuffer, []);
      expect(belowBuffer, allTestBytes());
    });

    test('error propagation', () async {
      StreamSink<List<int>> thresholdSink = new ThresholdSink(
          12,
          (Stream<List<int>> stream, _) => stream.fold(aboveBuffer, accumulate),
          (Stream<List<int>> stream, _) =>
              stream.fold(belowBuffer, accumulate));
      thresholdSink.add(testBytes1);
      await thresholdSink.addStream(new Stream.fromIterable(testBytes2));
      thresholdSink.add(testBytes3);
      thresholdSink.addError('yikes, an error!');

      try {
        await thresholdSink.close();
      } catch (e) {
        expect(e, 'yikes, an error!');
      }
    });
  });
}
