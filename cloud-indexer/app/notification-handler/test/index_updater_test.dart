// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:cloud_indexer_common/wrappers.dart';
import 'package:indexer_pipeline/index.dart';
import 'package:indexer_pipeline/render_json.dart';
import 'package:mockito/mockito.dart';
import 'package:notification_handler/index_updater.dart';
import 'package:parser/manifest.dart';
import 'package:test/test.dart';

class MockBucketWrapper extends Mock implements StorageBucketWrapper {}

main() {
  group('updateManifestUri', () {
    const String testBucketName = 'test_bucket_name';
    const String testArch = 'linux-x64';
    const String testRevision = 'b54f77abb289dcf2e39bc6f78ecab189aaf77a89';
    const String testPrefix = 'https://storage.googleapis.com/$testBucketName/'
        'services/$testArch/$testRevision/';

    const String testManifest1 = '#!mojo https://tq.mojoapps.io/handler.mojo\n'
        'title: Test Module 1\n'
        'arch: $testArch\n'
        'modularRevision: $testRevision\n'
        'icon: https://tq.mojoapps.io/test_module_1/icon.png\n'
        'url: https://tq.mojoapps.io/test_module_1.mojo\n'
        'verb: https://discover.io';

    const String testManifest2 = '#!mojo https://tq.mojoapps.io/handler.mojo\n'
        'title: Test Module 2\n'
        'arch: $testArch\n'
        'modularRevision: $testRevision\n'
        'url: https://tq.mojoapps.io/test_module_2.mojo\n'
        'verb: https://find.io';

    test('Update both URI and icon.', () async {
      Manifest manifest = new Manifest.parseYamlString(testManifest1);
      Manifest updatedManifest = updateManifestUri(manifest, testBucketName);
      expect(updatedManifest.url, Uri.parse('${testPrefix}test_module_1.mojo'));
      expect(updatedManifest.icon,
          Uri.parse('${testPrefix}test_module_1/icon.png'));
    });

    test('Update icon.', () async {
      Manifest manifest = new Manifest.parseYamlString(testManifest2);
      Manifest updatedManifest = updateManifestUri(manifest, testBucketName);
      expect(updatedManifest.url, Uri.parse('${testPrefix}test_module_2.mojo'));
      expect(updatedManifest.icon, null);
    });
  });

  group('updateIndex', () {
    const String testBucketName = 'test_bucket_name';
    const String testArch = 'linux-x64';
    const String testRevision = 'b54f77abb289dcf2e39bc6f78ecab189aaf77a89';
    const String testDirectory = 'services/$testArch/$testRevision/';
    const String testIndexPath = '${testDirectory}index.json';
    const String testPrefix =
        'https://storage.googleapis.com/$testBucketName/$testDirectory';

    const String testGeneration = '100001';

    const String validManifest = '#!mojo https://tq.mojoapps.io/handler.mojo\n'
        'title: Valid Manifest\n'
        'arch: $testArch\n'
        'modularRevision: $testRevision\n'
        'icon: https://tq.mojoapps.io/test_module_1/icon.png\n'
        'url: https://tq.mojoapps.io/test_module_1.mojo\n'
        'verb: https://discover.io';

    const String updatedValidManifest =
        '#!mojo https://tq.mojoapps.io/handler.mojo\n'
        'title: Valid Manifest\n'
        'arch: $testArch\n'
        'modularRevision: $testRevision\n'
        'icon: ${testPrefix}test_module_1/icon.png\n'
        'url: ${testPrefix}test_module_1.mojo\n'
        'verb: https://discover.io';

    // This jsonManifest is malformed because it is missing its arch field.
    const String malformedJsonManifest = '{'
        '"title":"Test Entry",'
        '"url":"https://test.io/module.mojo",'
        '"icon":"https://test.io/icon.png",'
        '"themeColor":null,'
        '"use":{},'
        '"verb":'
        '    {"label":{"uri":"https://seek.io","shorthand":"https://seek.io"}},'
        '"input":[],'
        '"output":[],'
        '"compose":[],'
        '"display":[],'
        '"schemas":[],'
        '"modularRevision":"$testRevision"'
        '}';

    const String jsonIndex = '[{'
        '"title":"Test Entry",'
        '"url":"https://test.io/module.mojo",'
        '"icon":"https://test.io/icon.png",'
        '"themeColor":null,'
        '"use":{},'
        '"verb":'
        '    {"label":{"uri":"https://seek.io","shorthand":"https://seek.io"}},'
        '"input":[],'
        '"output":[],'
        '"compose":[],'
        '"display":[],'
        '"schemas":[],'
        '"arch":"$testArch",'
        '"modularRevision":"$testRevision"'
        '}]';

    String getJsonManifest(String yamlManifest) =>
        new Manifest.parseYamlString(yamlManifest).toJsonString();
    final Matcher throwsManifestException =
        throwsA(new isInstanceOf<ManifestException>());
    final Matcher throwsCloudStorageFailureException =
        throwsA(new isInstanceOf<CloudStorageFailureException>());
    final Matcher throwsAtomicUpdateFailureException =
        throwsA(new isInstanceOf<AtomicUpdateFailureException>());

    test('Malformed manifest.', () {
      StorageBucketWrapper storageBucketWrapper = new MockBucketWrapper();
      when(storageBucketWrapper.bucketName).thenReturn(testBucketName);

      IndexUpdater indexUpdater = new IndexUpdater(storageBucketWrapper);
      expect(
          indexUpdater.update(malformedJsonManifest), throwsManifestException);
    });

    test('Non-existent index.', () async {
      StorageBucketWrapper storageBucketWrapper = new MockBucketWrapper();
      when(storageBucketWrapper.getObjectGeneration(testIndexPath)).thenAnswer(
          (i) => throw new DetailedApiRequestError(
              HttpStatus.NOT_FOUND, 'Resource not found.'));
      when(storageBucketWrapper.readObject(testIndexPath, generation: any))
          .thenAnswer((i) => throw new DetailedApiRequestError(
              HttpStatus.NOT_FOUND, 'Resource not found.'));
      when(storageBucketWrapper.bucketName).thenReturn(testBucketName);

      Index index = new Index();
      index.addManifest(updatedValidManifest);
      String updatedJsonIndex = renderJsonIndex(index);

      IndexUpdater indexUpdater = new IndexUpdater(storageBucketWrapper);
      await indexUpdater.update(getJsonManifest(validManifest));

      // A generation of '0' indicates that the object must not already be in
      // cloud storage. Also, the object should be written using
      // writeObjectAsBytes as the index is a smaller object.
      List<int> bytes = verify(storageBucketWrapper
              .writeObjectAsBytes(testIndexPath, captureAny, generation: '0'))
          .captured
          .single;
      expect(UTF8.decode(bytes), updatedJsonIndex);
    });

    test('Error fetching index.', () async {
      StorageBucketWrapper storageBucketWrapper = new MockBucketWrapper();
      when(storageBucketWrapper.getObjectGeneration(testIndexPath)).thenAnswer(
          (i) => throw new DetailedApiRequestError(
              HttpStatus.INTERNAL_SERVER_ERROR, 'Internal sever error.'));
      when(storageBucketWrapper.readObject(testIndexPath, generation: any))
          .thenAnswer((i) => throw new DetailedApiRequestError(
              HttpStatus.INTERNAL_SERVER_ERROR, 'Internal sever error.'));
      when(storageBucketWrapper.bucketName).thenReturn(testBucketName);

      IndexUpdater indexUpdater = new IndexUpdater(storageBucketWrapper);
      try {
        await indexUpdater.update(getJsonManifest(validManifest));
      } catch (e) {
        expect(e, new isInstanceOf<CloudStorageFailureException>());
      } finally {
        verifyNever(storageBucketWrapper.writeObjectAsBytes(testBucketName, any,
            generation: any));
      }
    });

    test('Error writing index.', () {
      StorageBucketWrapper storageBucketWrapper = new MockBucketWrapper();
      when(storageBucketWrapper.getObjectGeneration(testIndexPath))
          .thenReturn(testGeneration);
      when(storageBucketWrapper.readObject(testIndexPath, generation: any))
          .thenReturn(new Stream.fromIterable([jsonIndex.codeUnits]));
      when(storageBucketWrapper.writeObjectAsBytes(testIndexPath, any,
              generation: any))
          .thenAnswer((i) => throw new DetailedApiRequestError(
              HttpStatus.INTERNAL_SERVER_ERROR, 'Internal server error.'));
      when(storageBucketWrapper.bucketName).thenReturn(testBucketName);

      IndexUpdater indexUpdater = new IndexUpdater(storageBucketWrapper);
      expect(indexUpdater.update(getJsonManifest(validManifest)),
          throwsCloudStorageFailureException);
    });

    test('Valid index and manifest.', () async {
      StorageBucketWrapper storageBucketWrapper = new MockBucketWrapper();
      when(storageBucketWrapper.getObjectGeneration(testIndexPath))
          .thenReturn(testGeneration);
      when(storageBucketWrapper.readObject(testIndexPath, generation: any))
          .thenReturn(new Stream.fromIterable([jsonIndex.codeUnits]));
      when(storageBucketWrapper.writeObjectAsBytes(testIndexPath, any,
              generation: any))
          .thenReturn(testGeneration);
      when(storageBucketWrapper.bucketName).thenReturn(testBucketName);

      Index index = new Index();
      index.addJsonIndex(jsonIndex);
      index.addManifest(updatedValidManifest);
      String updatedJsonIndex = renderJsonIndex(index);

      IndexUpdater indexUpdater = new IndexUpdater(storageBucketWrapper);
      await indexUpdater.update(getJsonManifest(validManifest));

      List<int> bytes = verify(storageBucketWrapper.writeObjectAsBytes(
              testIndexPath, captureAny,
              generation: testGeneration))
          .captured
          .single;
      expect(UTF8.decode(bytes), updatedJsonIndex);
    });

    test('Atomic update failure: existing index.', () {
      StorageBucketWrapper storageBucketWrapper = new MockBucketWrapper();
      when(storageBucketWrapper.getObjectGeneration(testIndexPath))
          .thenReturn(testGeneration);
      when(storageBucketWrapper.readObject(testIndexPath,
              generation: testGeneration))
          .thenReturn(new Stream.fromIterable([jsonIndex.codeUnits]));
      when(storageBucketWrapper.writeObjectAsBytes(testIndexPath, any,
              generation: testGeneration))
          .thenAnswer((i) => throw new DetailedApiRequestError(
              HttpStatus.PRECONDITION_FAILED, 'Preconditions failed.'));
      when(storageBucketWrapper.bucketName).thenReturn(testBucketName);

      IndexUpdater indexUpdater = new IndexUpdater(storageBucketWrapper);
      expect(indexUpdater.update(getJsonManifest(validManifest)),
          throwsAtomicUpdateFailureException);
    });

    test('Atomic update failure: non-existent index.', () {
      StorageBucketWrapper storageBucketWrapper = new MockBucketWrapper();
      when(storageBucketWrapper.getObjectGeneration(testIndexPath)).thenAnswer(
          (i) => throw new DetailedApiRequestError(
              HttpStatus.NOT_FOUND, 'Resource not found.'));
      when(storageBucketWrapper.writeObjectAsBytes(testIndexPath, any,
              generation: '0'))
          .thenAnswer((i) => throw new DetailedApiRequestError(
              HttpStatus.PRECONDITION_FAILED, 'Preconditions failed.'));
      when(storageBucketWrapper.bucketName).thenReturn(testBucketName);

      IndexUpdater indexUpdater = new IndexUpdater(storageBucketWrapper);
      expect(indexUpdater.update(getJsonManifest(validManifest)),
          throwsAtomicUpdateFailureException);
    });
  });
}
