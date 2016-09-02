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
  group('StorageBucketWrapper', () {
    const String testBucketName = 'test-bucket.io';
    const String testObjectName = 'test_object.html';
    const String testGeneration = '123-test-456-generation';

    const List<int> testBytes1 = const [1, 2, 3];
    const List<int> testBytes2 = const [10, 11, 12];
    const List<List<int>> testBytes3 = const [
      const [4, 5, 6],
      const [7, 8, 9]
    ];

    storage_api.StorageApi storageApi;
    storage_api.ObjectsResourceApi objectsResourceApi;

    setUp(() {
      storageApi = new MockStorageApi();
      objectsResourceApi = new MockObjectsResourceApi();
      when(storageApi.objects).thenReturn(objectsResourceApi);
    });

    tearDown(() {
      storageApi = null;
      objectsResourceApi = null;
    });

    test('Happy route.', () async {
      when(objectsResourceApi.insert(null, testBucketName,
              name: testObjectName,
              ifGenerationMatch: testGeneration,
              uploadMedia: any,
              uploadOptions: storage_api.UploadOptions.Resumable))
          .thenReturn(new Future.value(
              new storage_api.Object()..generation = testGeneration));

      StorageBucketWrapper storageBucketWrapper =
          new StorageBucketWrapper.fromApi(storageApi, testBucketName);
      StreamSink<List<int>> sink = storageBucketWrapper
          .writeObject(testObjectName, generation: testGeneration);

      storage_api.Media media = verify(objectsResourceApi.insert(
              null, testBucketName,
              name: testObjectName,
              ifGenerationMatch: testGeneration,
              uploadMedia: captureAny,
              uploadOptions: storage_api.UploadOptions.Resumable))
          .captured
          .single;

      List<int> result = [];
      // Set up the subscription, otherwise the stream will never be consumed.
      media.stream.listen((List<int> data) {
        result.addAll(data);
      });

      sink.add(testBytes1);
      sink.add(testBytes2);
      await sink.addStream(new Stream.fromIterable(testBytes3));
      expect(await sink.close(), testGeneration);

      List<int> expected = <int>[]
        ..addAll(testBytes1)
        ..addAll(testBytes2)
        ..addAll(testBytes3.expand((i) => i));
      expect(result, expected);
    });
  });
}
