// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:googleapis/storage/v1.dart' as storage_api;
import 'package:indexer_pipeline/index.dart';
import 'package:indexer_pipeline/render_json.dart';
import 'package:mockito/mockito.dart';
import 'package:notification_handler/index_updater.dart';
import 'package:parser/manifest.dart';
import 'package:test/test.dart';

class MockApi extends Mock implements storage_api.StorageApi {}

class MockObjectsResourceApi extends Mock
    implements storage_api.ObjectsResourceApi {}

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
      storage_api.StorageApi api = new MockApi();
      storage_api.ObjectsResourceApi objectsResourceApi =
          new MockObjectsResourceApi();
      when(api.objects).thenReturn(objectsResourceApi);

      IndexUpdater indexUpdater = new IndexUpdater.fromApi(api, testBucketName);
      expect(
          indexUpdater.update(malformedJsonManifest), throwsManifestException);
    });

    test('Non-existent index.', () async {
      storage_api.StorageApi api = new MockApi();
      storage_api.ObjectsResourceApi objectsResourceApi =
          new MockObjectsResourceApi();
      when(api.objects).thenReturn(objectsResourceApi);

      when(objectsResourceApi.get(testBucketName, testIndexPath,
              projection: 'full'))
          .thenAnswer((i) => throw new storage_api.DetailedApiRequestError(
              HttpStatus.NOT_FOUND, 'Resource not found.'));

      Index index = new Index();
      index.addManifest(updatedValidManifest);
      String updatedJsonIndex = renderJsonIndex(index);

      IndexUpdater indexUpdater = new IndexUpdater.fromApi(api, testBucketName);
      await indexUpdater.update(getJsonManifest(validManifest));

      // A generation of '0' indicates that the object must not already be in
      // cloud storage.
      storage_api.Media resultingIndexData = verify(objectsResourceApi.insert(
              null, testBucketName,
              ifGenerationMatch: '0',
              name: testIndexPath,
              uploadMedia: captureAny,
              downloadOptions: storage_api.DownloadOptions.Metadata))
          .captured
          .single;

      expect(
          await UTF8.decodeStream(resultingIndexData.stream), updatedJsonIndex);
    });

    test('Error fetching index.', () {
      storage_api.StorageApi api = new MockApi();
      storage_api.ObjectsResourceApi objectsResourceApi =
          new MockObjectsResourceApi();
      when(api.objects).thenReturn(objectsResourceApi);

      when(objectsResourceApi.get(testBucketName, testIndexPath,
              projection: 'full'))
          .thenAnswer((i) => throw new storage_api.DetailedApiRequestError(
              HttpStatus.INTERNAL_SERVER_ERROR, 'Internal server error.'));

      IndexUpdater indexUpdater = new IndexUpdater.fromApi(api, testBucketName);
      expect(
          indexUpdater.update(getJsonManifest(validManifest)).whenComplete(() {
            verifyNever(objectsResourceApi.insert(null, testBucketName,
                ifGenerationMatch: any,
                name: testIndexPath,
                uploadMedia: captureAny,
                downloadOptions: storage_api.DownloadOptions.Metadata));
          }),
          throwsCloudStorageFailureException);
    });

    test('Error writing index.', () {
      storage_api.Object indexResource = new storage_api.Object();
      indexResource.generation = testGeneration;

      storage_api.Media indexData = new storage_api.Media(
          new Stream.fromIterable([jsonIndex.codeUnits]),
          jsonIndex.codeUnits.length);

      storage_api.StorageApi api = new MockApi();
      storage_api.ObjectsResourceApi objectsResourceApi =
          new MockObjectsResourceApi();
      when(api.objects).thenReturn(objectsResourceApi);

      when(objectsResourceApi.get(testBucketName, testIndexPath,
              projection: 'full'))
          .thenReturn(new Future.value(indexResource));
      when(objectsResourceApi.get(testBucketName, testIndexPath,
              ifGenerationMatch: testGeneration,
              downloadOptions: storage_api.DownloadOptions.FullMedia))
          .thenReturn(new Future.value(indexData));
      when(objectsResourceApi.insert(null, testBucketName,
              ifGenerationMatch: any,
              name: testIndexPath,
              uploadMedia: any,
              downloadOptions: storage_api.DownloadOptions.Metadata))
          .thenAnswer((i) => throw new storage_api.DetailedApiRequestError(
              HttpStatus.INTERNAL_SERVER_ERROR, 'Internal server error.'));

      IndexUpdater indexUpdater = new IndexUpdater.fromApi(api, testBucketName);
      expect(indexUpdater.update(getJsonManifest(validManifest)),
          throwsCloudStorageFailureException);
    });

    test('Valid index and manifest.', () async {
      storage_api.Object indexResource = new storage_api.Object();
      indexResource.generation = testGeneration;

      storage_api.Media indexData = new storage_api.Media(
          new Stream.fromIterable([jsonIndex.codeUnits]),
          jsonIndex.codeUnits.length);

      storage_api.StorageApi api = new MockApi();
      storage_api.ObjectsResourceApi objectsResourceApi =
          new MockObjectsResourceApi();
      when(api.objects).thenReturn(objectsResourceApi);

      when(objectsResourceApi.get(testBucketName, testIndexPath,
              projection: 'full'))
          .thenReturn(new Future.value(indexResource));
      when(objectsResourceApi.get(testBucketName, testIndexPath,
              ifGenerationMatch: testGeneration,
              downloadOptions: storage_api.DownloadOptions.FullMedia))
          .thenReturn(new Future.value(indexData));

      Index index = new Index();
      index.addJsonIndex(jsonIndex);
      index.addManifest(updatedValidManifest);
      String updatedJsonIndex = renderJsonIndex(index);

      IndexUpdater indexUpdater = new IndexUpdater.fromApi(api, testBucketName);
      await indexUpdater.update(getJsonManifest(validManifest));

      storage_api.Media resultingIndexData = verify(objectsResourceApi.insert(
              null, testBucketName,
              ifGenerationMatch: testGeneration,
              name: testIndexPath,
              uploadMedia: captureThat(new isInstanceOf<storage_api.Media>()),
              downloadOptions: storage_api.DownloadOptions.Metadata))
          .captured
          .single;

      expect(
          await UTF8.decodeStream(resultingIndexData.stream), updatedJsonIndex);
    });

    test('Atomic update failure: existing index.', () {
      storage_api.Object indexResource = new storage_api.Object();
      indexResource.generation = testGeneration;

      storage_api.Media indexData = new storage_api.Media(
          new Stream.fromIterable([jsonIndex.codeUnits]),
          jsonIndex.codeUnits.length);

      storage_api.StorageApi api = new MockApi();
      storage_api.ObjectsResourceApi objectsResourceApi =
          new MockObjectsResourceApi();
      when(api.objects).thenReturn(objectsResourceApi);

      when(objectsResourceApi.get(testBucketName, testIndexPath,
              projection: 'full'))
          .thenReturn(new Future.value(indexResource));
      when(objectsResourceApi.get(testBucketName, testIndexPath,
              ifGenerationMatch: testGeneration,
              downloadOptions: storage_api.DownloadOptions.FullMedia))
          .thenReturn(new Future.value(indexData));
      when(objectsResourceApi.insert(null, testBucketName,
              ifGenerationMatch: testGeneration,
              name: testIndexPath,
              uploadMedia: any,
              downloadOptions: storage_api.DownloadOptions.Metadata))
          .thenAnswer((i) => throw new storage_api.DetailedApiRequestError(
              HttpStatus.PRECONDITION_FAILED, 'Preconditions failed.'));

      IndexUpdater indexUpdater = new IndexUpdater.fromApi(api, testBucketName);
      expect(indexUpdater.update(getJsonManifest(validManifest)),
          throwsAtomicUpdateFailureException);
    });

    test('Atomic update failure: non-existent index.', () {
      storage_api.StorageApi api = new MockApi();
      storage_api.ObjectsResourceApi objectsResourceApi =
          new MockObjectsResourceApi();
      when(api.objects).thenReturn(objectsResourceApi);

      when(objectsResourceApi.get(testBucketName, testIndexPath,
              projection: 'full'))
          .thenAnswer((i) => throw new storage_api.DetailedApiRequestError(
              HttpStatus.NOT_FOUND, 'Resource not found.'));
      when(objectsResourceApi.insert(null, testBucketName,
              ifGenerationMatch: '0',
              name: testIndexPath,
              uploadMedia: any,
              downloadOptions: storage_api.DownloadOptions.Metadata))
          .thenAnswer((i) => throw new storage_api.DetailedApiRequestError(
              HttpStatus.PRECONDITION_FAILED, 'Preconditions failed.'));

      IndexUpdater indexUpdater = new IndexUpdater.fromApi(api, testBucketName);
      expect(indexUpdater.update(getJsonManifest(validManifest)),
          throwsAtomicUpdateFailureException);
    });
  });
}
