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
    const String testManifest1 = '#!mojo https://tq.mojoapps.io/handler.mojo\n'
        'title: Test Module 1\n'
        'icon: https://tq.mojoapps.io/test_module_1/icon.png\n'
        'url: https://tq.mojoapps.io/test_module_1.mojo\n'
        'verb: https://discover.io';

    const String testManifest2 = '#!mojo https://tq.mojoapps.io/handler.mojo\n'
        'title: Test Module 2\n'
        'url: https://tq.mojoapps.io/test_module_2.mojo\n'
        'verb: https://find.io';

    const String testBucketName = 'test_bucket_name';
    const String testArch = 'test_arch';
    const String testRevision = 'test_revision';
    const String testPrefix = 'https://storage.googleapis.com/$testBucketName/'
        'services/$testArch/$testRevision/';

    test('Update both URI and icon.', () async {
      Manifest manifest = new Manifest.parseYamlString(testManifest1);
      Manifest updatedManifest =
          updateManifestUri(manifest, testBucketName, testArch, testRevision);
      expect(updatedManifest.url, Uri.parse('${testPrefix}test_module_1.mojo'));
      expect(updatedManifest.icon,
          Uri.parse('${testPrefix}test_module_1/icon.png'));
    });

    test('Update icon.', () async {
      Manifest manifest = new Manifest.parseYamlString(testManifest2);
      Manifest updatedManifest =
          updateManifestUri(manifest, testBucketName, testArch, testRevision);
      expect(updatedManifest.url, Uri.parse('${testPrefix}test_module_2.mojo'));
      expect(updatedManifest.icon, null);
    });
  });

  group('updateIndex', () {
    const String testBucketName = 'test_bucket_name';
    const String testArch = 'test_arch';
    const String testRevision = 'test_revision';
    const String testDirectory = 'services/$testArch/$testRevision/';
    const String testManifestPath = '${testDirectory}test_manifest.yaml';
    const String testIndexPath = '${testDirectory}index.json';
    const String testPrefix =
        'https://storage.googleapis.com/$testBucketName/$testDirectory';

    const String testGeneration = '100001';

    // This is a malformed manifest because 'uri' should be 'url'.
    const String malformedManifest =
        '#!mojo https://tq.mojoapps.io/handler.mojo\n'
        'title: Malformed Manifest\n'
        'icon: https://tq.mojoapps.io/test_module_1/icon.png\n'
        'uri: https://tq.mojoapps.io/test_module_1.mojo\n'
        'verb: https://discover.io';
    const String validManifest = '#!mojo https://tq.mojoapps.io/handler.mojo\n'
        'title: Valid Manifest\n'
        'icon: https://tq.mojoapps.io/test_module_1/icon.png\n'
        'url: https://tq.mojoapps.io/test_module_1.mojo\n'
        'verb: https://discover.io';
    const String updatedValidManifest =
        '#!mojo https://tq.mojoapps.io/handler.mojo\n'
        'title: Valid Manifest\n'
        'icon: ${testPrefix}test_module_1/icon.png\n'
        'url: ${testPrefix}test_module_1.mojo\n'
        'verb: https://discover.io';
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
        '"schemas":[]}]';

    test('Malformed manifest.', () async {
      storage_api.Object indexResource = new storage_api.Object();
      indexResource.generation = testGeneration;

      storage_api.Media indexData = new storage_api.Media(
          new Stream.fromIterable([jsonIndex.codeUnits]),
          jsonIndex.codeUnits.length);
      storage_api.Media manifestData = new storage_api.Media(
          new Stream.fromIterable([malformedManifest.codeUnits]),
          malformedManifest.codeUnits.length);

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
      when(objectsResourceApi.get(testBucketName, testManifestPath,
              ifGenerationMatch: null,
              downloadOptions: storage_api.DownloadOptions.FullMedia))
          .thenReturn(new Future.value(manifestData));

      IndexUpdater indexUpdater = new IndexUpdater.fromApi(api, testBucketName);
      expect(indexUpdater.update(testManifestPath, testArch, testRevision),
          throwsA(new isInstanceOf<ManifestException>()));

      // Ensure that there were no state changes.
      verifyNever(objectsResourceApi.insert(null, testBucketName,
          ifGenerationMatch: testGeneration, uploadMedia: any));
    });

    test('Failed to fetch manifest.', () async {
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
      when(objectsResourceApi.get(testBucketName, testManifestPath,
              ifGenerationMatch: null,
              downloadOptions: storage_api.DownloadOptions.FullMedia))
          .thenAnswer((i) => throw new storage_api.DetailedApiRequestError(
              HttpStatus.GATEWAY_TIMEOUT, 'Gateway timeout. Try again later.'));

      IndexUpdater indexUpdater = new IndexUpdater.fromApi(api, testBucketName);
      expect(indexUpdater.update(testManifestPath, testArch, testRevision),
          throwsA(new isInstanceOf<CloudStorageFailureException>()));

      // Ensure that there were no state changes.
      verifyNever(objectsResourceApi.insert(null, testBucketName,
          name: testIndexPath,
          ifGenerationMatch: testGeneration,
          uploadMedia: any,
          downloadOptions: storage_api.DownloadOptions.Metadata));
    });

    test('Non-existent index.', () async {
      storage_api.Media manifestData = new storage_api.Media(
          new Stream.fromIterable([validManifest.codeUnits]),
          validManifest.codeUnits.length);

      storage_api.StorageApi api = new MockApi();
      storage_api.ObjectsResourceApi objectsResourceApi =
          new MockObjectsResourceApi();
      when(api.objects).thenReturn(objectsResourceApi);

      when(objectsResourceApi.get(testBucketName, testIndexPath,
              projection: 'full'))
          .thenAnswer((i) => throw new storage_api.DetailedApiRequestError(
              HttpStatus.NOT_FOUND, 'Resource not found.'));
      when(objectsResourceApi.get(testBucketName, testManifestPath,
              ifGenerationMatch: null,
              downloadOptions: storage_api.DownloadOptions.FullMedia))
          .thenReturn(new Future.value(manifestData));

      Index index = new Index();
      index.addManifest(updatedValidManifest);
      String updatedJsonIndex = renderJsonIndex(index);

      IndexUpdater indexUpdater = new IndexUpdater.fromApi(api, testBucketName);
      await indexUpdater.update(testManifestPath, testArch, testRevision);

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

    test('Valid index and manifest.', () async {
      storage_api.Object indexResource = new storage_api.Object();
      indexResource.generation = testGeneration;

      storage_api.Media indexData = new storage_api.Media(
          new Stream.fromIterable([jsonIndex.codeUnits]),
          jsonIndex.codeUnits.length);
      storage_api.Media manifestData = new storage_api.Media(
          new Stream.fromIterable([validManifest.codeUnits]),
          validManifest.codeUnits.length);

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
      when(objectsResourceApi.get(testBucketName, testManifestPath,
              ifGenerationMatch: null,
              downloadOptions: storage_api.DownloadOptions.FullMedia))
          .thenReturn(new Future.value(manifestData));

      Index index = new Index();
      index.addJsonIndex(jsonIndex);
      index.addManifest(updatedValidManifest);
      String updatedJsonIndex = renderJsonIndex(index);

      IndexUpdater indexUpdater = new IndexUpdater.fromApi(api, testBucketName);
      await indexUpdater.update(testManifestPath, testArch, testRevision);

      storage_api.Media resultingIndexData = verify(objectsResourceApi.insert(
              null, testBucketName,
              ifGenerationMatch: testGeneration,
              name: testIndexPath,
              uploadMedia: captureAny,
              downloadOptions: storage_api.DownloadOptions.Metadata))
          .captured
          .single;

      expect(
          await UTF8.decodeStream(resultingIndexData.stream), updatedJsonIndex);
    });

    test('Atomic update failure: existing index.', () async {
      storage_api.Object indexResource = new storage_api.Object();
      indexResource.generation = testGeneration;

      storage_api.Media indexData = new storage_api.Media(
          new Stream.fromIterable([jsonIndex.codeUnits]),
          jsonIndex.codeUnits.length);
      storage_api.Media manifestData = new storage_api.Media(
          new Stream.fromIterable([validManifest.codeUnits]),
          validManifest.codeUnits.length);

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
      when(objectsResourceApi.get(testBucketName, testManifestPath,
              ifGenerationMatch: null,
              downloadOptions: storage_api.DownloadOptions.FullMedia))
          .thenReturn(new Future.value(manifestData));
      when(objectsResourceApi.insert(null, testBucketName,
              ifGenerationMatch: testGeneration,
              name: testIndexPath,
              uploadMedia: captureAny,
              downloadOptions: storage_api.DownloadOptions.Metadata))
          .thenAnswer((i) => throw new storage_api.DetailedApiRequestError(
              HttpStatus.PRECONDITION_FAILED, 'Preconditions failed.'));

      IndexUpdater indexUpdater = new IndexUpdater.fromApi(api, testBucketName);
      expect(indexUpdater.update(testManifestPath, testArch, testRevision),
          throwsA(new isInstanceOf<AtomicUpdateFailureException>()));
    });

    test('Atomic update failure: non-existent index.', () async {
      storage_api.Media manifestData = new storage_api.Media(
          new Stream.fromIterable([validManifest.codeUnits]),
          validManifest.codeUnits.length);

      storage_api.StorageApi api = new MockApi();
      storage_api.ObjectsResourceApi objectsResourceApi =
          new MockObjectsResourceApi();
      when(api.objects).thenReturn(objectsResourceApi);

      when(objectsResourceApi.get(testBucketName, testIndexPath,
              projection: 'full'))
          .thenAnswer((i) => throw new storage_api.DetailedApiRequestError(
              HttpStatus.NOT_FOUND, 'Resource not found.'));
      when(objectsResourceApi.get(testBucketName, testManifestPath,
              ifGenerationMatch: null,
              downloadOptions: storage_api.DownloadOptions.FullMedia))
          .thenReturn(new Future.value(manifestData));
      when(objectsResourceApi.insert(null, testBucketName,
              ifGenerationMatch: '0',
              name: testIndexPath,
              uploadMedia: captureAny,
              downloadOptions: storage_api.DownloadOptions.Metadata))
          .thenAnswer((i) => throw new storage_api.DetailedApiRequestError(
              HttpStatus.PRECONDITION_FAILED, 'Preconditions failed.'));

      IndexUpdater indexUpdater = new IndexUpdater.fromApi(api, testBucketName);
      expect(indexUpdater.update(testManifestPath, testArch, testRevision),
          throwsA(new isInstanceOf<AtomicUpdateFailureException>()));
    });
  });
}
