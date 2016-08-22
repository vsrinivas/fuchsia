// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:appengine/appengine.dart';
import 'package:gcloud/storage.dart';

// TODO(victorkwan): Swap this out once it is exposed by a 'public' API,
import 'package:_discoveryapis_commons/_discoveryapis_commons.dart';

import 'package:indexer_pipeline/index.dart';
import 'package:indexer_pipeline/render_json.dart';
import 'package:mockito/mockito.dart';
import 'package:notification_handler/src/request_handler.dart' as rh;
import 'package:parser/manifest.dart';
import 'package:test/test.dart';

class MockBucket extends Mock implements Bucket {}

class MockLogging extends Mock implements Logging {}

class MockRequest extends Mock implements HttpRequest {}

class MockResponse extends Mock implements HttpResponse {}

Future<Null> main() async {
  group('updateManifestUri', () {
    const String testManifest1 = '#!mojo https://tq.mojoapps.io/handler.mojo\n'
        'title: Test Module 1\n'
        'icon: https://tq.mojoapps.io/test_module_1/icon.png\n'
        'url: https://tq.mojoapps.io/test_module_1.mojo\n'
        'verb: https://discover.io';

    final String testManifest2 = '#!mojo https://tq.mojoapps.io/handler.mojo\n'
        'title: Test Module 2\n'
        'url: https://tq.mojoapps.io/test_module_2.mojo\n'
        'verb: https://find.io';

    final String testBucketName = 'test_bucket_name';
    final String testArch = 'test_arch';
    final String testRevision = 'test_revision';
    final String testPrefix = 'https://storage.googleapis.com/$testBucketName/'
        'services/$testArch/$testRevision/';

    test('Update both URI and icon.', () async {
      Manifest manifest = new Manifest.parseYamlString(testManifest1);
      Manifest updatedManifest =
          rh.updateManifestUri(manifest, testBucketName, testArch, testRevision);
      expect(updatedManifest.url, Uri.parse('${testPrefix}test_module_1.mojo'));
      expect(
          updatedManifest.icon, Uri.parse('${testPrefix}test_module_1/icon.png'));
    });

    test('Update icon.', () async {
      Manifest manifest = new Manifest.parseYamlString(testManifest2);
      Manifest updatedManifest =
          rh.updateManifestUri(manifest, testBucketName, testArch, testRevision);
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
      Bucket bucket = new MockBucket();
      Logging logging = new MockLogging();
      when(bucket.bucketName).thenReturn(testBucketName);
      when(bucket.read(testIndexPath)).thenAnswer((i) =>
          throw new DetailedApiRequestError(
              HttpStatus.NOT_FOUND, 'Resource not found.'));
      when(bucket.read(testManifestPath))
          .thenReturn(new Stream.fromIterable([malformedManifest.codeUnits]));

      HttpRequest request = new MockRequest();
      HttpResponse response = new MockResponse();
      when(request.response).thenReturn(response);

      await rh.updateIndex(
          request, bucket, logging, testManifestPath, testArch, testRevision);

      // Make sure there were no state changes.
      verifyNever(bucket.write(testIndexPath));

      // The statusCode would be set to OK, to indicate not to send again.
      verify(response.statusCode = HttpStatus.OK);
    });

    test('Failed to fetch manifest.', () async {
      Bucket bucket = new MockBucket();
      Logging logging = new MockLogging();
      when(bucket.bucketName).thenReturn(testBucketName);
      when(bucket.read(testIndexPath))
          .thenReturn(new Stream.fromIterable([jsonIndex.codeUnits]));
      when(bucket.read(testManifestPath)).thenAnswer((i) =>
          throw new DetailedApiRequestError(
              HttpStatus.GATEWAY_TIMEOUT, 'Gateway timeout. Try again later.'));

      HttpRequest request = new MockRequest();
      HttpResponse response = new MockResponse();
      when(request.response).thenReturn(response);

      await rh.updateIndex(
          request, bucket, logging, testManifestPath, testArch, testRevision);

      // Make sure there were no state changes.
      verifyNever(bucket.write(testIndexPath));

      // An error code of >299 indicates that we should try again later.
      verify(response.statusCode = captureThat(greaterThan(299)));
    });

    test('Non-existent index.', () async {
      Bucket bucket = new MockBucket();
      Logging logging = new MockLogging();
      when(bucket.bucketName).thenReturn(testBucketName);
      when(bucket.read(testManifestPath))
          .thenReturn(new Stream.fromIterable([validManifest.codeUnits]));
      when(bucket.read(testIndexPath)).thenAnswer((i) =>
          throw new DetailedApiRequestError(
              HttpStatus.NOT_FOUND, 'Resource not found.'));

      HttpRequest request = new MockRequest();
      HttpResponse response = new MockResponse();
      when(request.response).thenReturn(response);

      await rh.updateIndex(
          request, bucket, logging, testManifestPath, testArch, testRevision);

      Index index = new Index();
      index.addManifest(updatedValidManifest);
      String updatedJsonIndex = renderJsonIndex(index);

      verify(bucket.writeBytes(testIndexPath, updatedJsonIndex.codeUnits));
      verify(response.statusCode = HttpStatus.OK);
    });

    test('Valid index and manifest.', () async {
      Bucket bucket = new MockBucket();
      Logging logging = new MockLogging();
      when(bucket.bucketName).thenReturn(testBucketName);
      when(bucket.read(testManifestPath))
          .thenReturn(new Stream.fromIterable([validManifest.codeUnits]));
      when(bucket.read(testIndexPath))
          .thenReturn(new Stream.fromIterable([jsonIndex.codeUnits]));

      HttpRequest request = new MockRequest();
      HttpResponse response = new MockResponse();
      when(request.response).thenReturn(response);

      await rh.updateIndex(
          request, bucket, logging, testManifestPath, testArch, testRevision);

      Index index = new Index();
      index.addJsonIndex(jsonIndex);
      index.addManifest(updatedValidManifest);
      String updatedJsonIndex = renderJsonIndex(index);

      verify(bucket.writeBytes(testIndexPath, updatedJsonIndex.codeUnits));
      verify(response.statusCode = HttpStatus.OK);
    });
  });
}
