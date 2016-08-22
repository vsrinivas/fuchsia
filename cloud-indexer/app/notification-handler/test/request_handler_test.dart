// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:mockito/mockito.dart';
import 'package:notification_handler/index_updater.dart';
import 'package:notification_handler/request_handler.dart';
import 'package:shelf/shelf.dart' as shelf;
import 'package:test/test.dart';

class MockIndexUpdater extends Mock implements IndexUpdater {}

String _createChangeNotification(
    String name, String arch, String revision, String resourceState) {
  return JSON.encode({
    'name': name,
    'arch': arch,
    'revision': revision,
    'resource_state': resourceState
  });
}

main() {
  group('requestHandler', () {
    final Uri testUri = Uri.parse('https://test-notification-handler.io/');
    const String testArch = 'test_arch';
    const String testRevision = 'test_revision';
    const String testName =
        'services/$testArch/$testRevision/test_manifest.yaml';

    test('Invalid queue name.', () async {
      IndexUpdater indexUpdater = new MockIndexUpdater();
      shelf.Request request = new shelf.Request('POST', testUri,
          headers: {'X-AppEngine-QueueName': 'not-modular-indexing'});
      shelf.Response response =
          await requestHandler(request, indexUpdater: indexUpdater);
      expect(response.statusCode, HttpStatus.OK);
    });

    test('"not_exists" request.', () async {
      IndexUpdater indexUpdater = new MockIndexUpdater();
      shelf.Request request = new shelf.Request('POST', testUri,
          headers: {'X-AppEngine-QueueName': 'indexing'},
          body: _createChangeNotification(
              testName, testArch, testRevision, 'not_exists'));
      shelf.Response response =
          await requestHandler(request, indexUpdater: indexUpdater);
      expect(response.statusCode, HttpStatus.OK);
    });

    test('Atomic guarantee failure.', () async {
      IndexUpdater indexUpdater = new MockIndexUpdater();
      when(indexUpdater.update(testName, testArch, testRevision)).thenAnswer(
          (i) => throw new AtomicUpdateFailureException(
              'Atomic guarantees failed.'));
      shelf.Request request = new shelf.Request('POST', testUri,
          headers: {'X-AppEngine-QueueName': 'indexing'},
          body: _createChangeNotification(
              testName, testArch, testRevision, 'exists'));
      shelf.Response response =
          await requestHandler(request, indexUpdater: indexUpdater);
      expect(response.statusCode, greaterThan(299));
    });

    test('Cloud storage failure.', () async {
      IndexUpdater indexUpdater = new MockIndexUpdater();
      when(indexUpdater.update(testName, testArch, testRevision)).thenAnswer(
          (i) => throw new CloudStorageFailureException(
              'Internal server error.', HttpStatus.INTERNAL_SERVER_ERROR));
      shelf.Request request = new shelf.Request('POST', testUri,
          headers: {'X-AppEngine-QueueName': 'indexing'},
          body: _createChangeNotification(
              testName, testArch, testRevision, 'exists'));
      shelf.Response response =
          await requestHandler(request, indexUpdater: indexUpdater);
      expect(response.statusCode, greaterThan(299));
    });

    test('Manifest exception.', () async {
      IndexUpdater indexUpdater = new MockIndexUpdater();
      when(indexUpdater.update(testName, testArch, testRevision)).thenAnswer(
          (i) => throw new ManifestException('Manifest not found (404).'));
      shelf.Request request = new shelf.Request('POST', testUri,
          headers: {'X-AppEngine-QueueName': 'indexing'},
          body: _createChangeNotification(
              testName, testArch, testRevision, 'exists'));
      shelf.Response response =
          await requestHandler(request, indexUpdater: indexUpdater);
      expect(response.statusCode, HttpStatus.OK);
    });

    test('Valid notification.', () async {
      IndexUpdater indexUpdater = new MockIndexUpdater();
      when(indexUpdater.update(testName, testArch, testRevision))
          .thenReturn(new Future.value(null));
      shelf.Request request = new shelf.Request('POST', testUri,
          headers: {'X-AppEngine-QueueName': 'indexing'},
          body: _createChangeNotification(
              testName, testArch, testRevision, 'exists'));
      shelf.Response response =
          await requestHandler(request, indexUpdater: indexUpdater);
      expect(response.statusCode, HttpStatus.OK);
    });
  });
}
