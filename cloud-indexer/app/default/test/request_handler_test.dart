// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:cloud_indexer/auth_manager.dart';
import 'package:cloud_indexer/module_uploader.dart';
import 'package:cloud_indexer/request_handler.dart';
import 'package:cloud_indexer/tarball.dart';
import 'package:mockito/mockito.dart';
import 'package:shelf/shelf.dart' as shelf;
import 'package:test/test.dart';

shelf.Request multipartRequest(
    Uri uri, String boundary, String name, List<int> bytes,
    {Map<String, String> additionalHeaders: const {}}) {
  final Map<String, String> headers = {
    'Content-Type': 'multipart/form-data; boundary="$boundary"'
  };
  headers.addAll(additionalHeaders);

  final Iterable<List<int>> requestBody = [
    '--$boundary\r\n',
    'Content-Type: application/octet-stream\r\n',
    'Content-Disposition: form-data; name="$name"\r\n\r\n',
    bytes,
    '\r\n--$boundary--\r\n\r\n'
  ].map((i) => (i is String) ? ASCII.encode(i) : i);

  return new shelf.Request('POST', uri,
      headers: headers, body: new Stream.fromIterable(requestBody));
}

class MockModuleUploader extends Mock implements ModuleUploader {}

class MockAuthManager extends Mock implements AuthManager {}

main() {
  group('requestHandler', () {
    const String testName = 'module';
    const String testBoundary = 'gc0p4Jq0M2Yt08jU534c0p';
    const Map<String, String> testHeaders = const {
      'Authorization': 'Bearer test-token'
    };
    const String testEmail = 'test.account@mojoapps.io';
    final List<int> testBytes = UTF8.encode('This is a test.');
    final Uri defaultUri = Uri.parse('https://default-service.io/api/upload');

    test('Unauthorized request.', () async {
      ModuleUploader moduleUploader = new MockModuleUploader();
      MockAuthManager authManager = new MockAuthManager();
      when(authManager.authenticatedUser(any)).thenReturn(null);

      shelf.Request request = new shelf.Request('POST', defaultUri);
      shelf.Response response = await requestHandler(request,
          moduleUploader: moduleUploader, authManager: authManager);
      expect(response.statusCode, HttpStatus.FORBIDDEN);

      // Ensure that nothing was processed.
      verifyNever(moduleUploader.processUpload(any));
    });

    test('Incorrect path.', () async {
      ModuleUploader moduleUploader = new MockModuleUploader();
      MockAuthManager authManager = new MockAuthManager();
      when(authManager.authenticatedUser(testHeaders['Authorization']))
          .thenReturn(testEmail);

      shelf.Request request = new shelf.Request(
          'POST', Uri.parse('https://default-service.io/'),
          headers: testHeaders);
      shelf.Response response = await requestHandler(request,
          moduleUploader: moduleUploader, authManager: authManager);
      expect(response.statusCode, HttpStatus.NOT_FOUND);

      // Ensure that nothing was processed.
      verifyNever(moduleUploader.processUpload(any));
    });

    test('Invalid boundary.', () async {
      ModuleUploader moduleUploader = new MockModuleUploader();
      MockAuthManager authManager = new MockAuthManager();
      when(authManager.authenticatedUser(testHeaders['Authorization']))
          .thenReturn(testEmail);

      shelf.Request request = multipartRequest(
          defaultUri, 'test"boundary', testName, testBytes,
          additionalHeaders: testHeaders);
      shelf.Response response = await requestHandler(request,
          moduleUploader: moduleUploader, authManager: authManager);
      expect(response.statusCode, HttpStatus.BAD_REQUEST);
      verifyNever(moduleUploader.processUpload(any));
    });

    test('Missing tarball.', () async {
      ModuleUploader moduleUploader = new MockModuleUploader();
      MockAuthManager authManager = new MockAuthManager();
      when(authManager.authenticatedUser(testHeaders['Authorization']))
          .thenReturn(testEmail);

      shelf.Request request = multipartRequest(
          defaultUri, testBoundary, 'file', testBytes,
          additionalHeaders: testHeaders);
      shelf.Response response = await requestHandler(request,
          moduleUploader: moduleUploader, authManager: authManager);
      expect(response.statusCode, HttpStatus.BAD_REQUEST);
      verifyNever(moduleUploader.processUpload(any));
    });

    test('Cloud storage failure.', () async {
      ModuleUploader moduleUploader = new MockModuleUploader();
      when(moduleUploader.processUpload(any)).thenAnswer((i) =>
          throw new CloudStorageException(
              HttpStatus.INTERNAL_SERVER_ERROR, 'Internal server error.'));

      MockAuthManager authManager = new MockAuthManager();
      when(authManager.authenticatedUser(testHeaders['Authorization']))
          .thenReturn(testEmail);

      shelf.Request request = multipartRequest(
          defaultUri, testBoundary, testName, testBytes,
          additionalHeaders: testHeaders);
      shelf.Response response = await requestHandler(request,
          moduleUploader: moduleUploader, authManager: authManager);
      expect(response.statusCode, HttpStatus.INTERNAL_SERVER_ERROR);

      // Verify that the payload was correctly parsed.
      Stream<List<int>> data =
          verify(moduleUploader.processUpload(captureAny)).captured.single;
      BytesBuilder bytesBuilder = await data.fold(
          new BytesBuilder(),
          (BytesBuilder bytesBuilder, List<int> bytes) =>
              bytesBuilder..add(bytes));
      expect(bytesBuilder.toBytes(), testBytes);
    });

    test('Pub/Sub failure.', () async {
      ModuleUploader moduleUploader = new MockModuleUploader();
      when(moduleUploader.processUpload(any)).thenAnswer((i) =>
          throw new CloudStorageException(
              HttpStatus.INTERNAL_SERVER_ERROR, 'Internal server error.'));

      MockAuthManager authManager = new MockAuthManager();
      when(authManager.authenticatedUser(testHeaders['Authorization']))
          .thenReturn(testEmail);

      shelf.Request request = multipartRequest(
          defaultUri, testBoundary, testName, testBytes,
          additionalHeaders: testHeaders);
      shelf.Response response = await requestHandler(request,
          moduleUploader: moduleUploader, authManager: authManager);
      expect(response.statusCode, HttpStatus.INTERNAL_SERVER_ERROR);

      Stream<List<int>> data =
          verify(moduleUploader.processUpload(captureAny)).captured.single;
      BytesBuilder bytesBuilder = await data.fold(
          new BytesBuilder(),
          (BytesBuilder bytesBuilder, List<int> bytes) =>
              bytesBuilder..add(bytes));
      expect(bytesBuilder.toBytes(), testBytes);
    });

    test('Tarball failure.', () async {
      ModuleUploader moduleUploader = new MockModuleUploader();
      when(moduleUploader.processUpload(any)).thenAnswer((i) =>
          throw new TarballException('Tarball did not contain a manifest.'));

      MockAuthManager authManager = new MockAuthManager();
      when(authManager.authenticatedUser(testHeaders['Authorization']))
          .thenReturn(testEmail);

      shelf.Request request = multipartRequest(
          defaultUri, testBoundary, testName, testBytes,
          additionalHeaders: testHeaders);
      shelf.Response response = await requestHandler(request,
          moduleUploader: moduleUploader, authManager: authManager);

      // In general, we fault the requester should we have a TarballException.
      expect(response.statusCode, HttpStatus.BAD_REQUEST);

      Stream<List<int>> data =
          verify(moduleUploader.processUpload(captureAny)).captured.single;
      BytesBuilder bytesBuilder = await data.fold(
          new BytesBuilder(),
          (BytesBuilder bytesBuilder, List<int> bytes) =>
              bytesBuilder..add(bytes));
      expect(bytesBuilder.toBytes(), testBytes);
    });

    test('Valid request.', () async {
      ModuleUploader moduleUploader = new MockModuleUploader();
      when(moduleUploader.processUpload(any))
          .thenReturn(new Future.value(null));

      MockAuthManager authManager = new MockAuthManager();
      when(authManager.authenticatedUser(testHeaders['Authorization']))
          .thenReturn(testEmail);

      shelf.Request request = multipartRequest(
          defaultUri, testBoundary, testName, testBytes,
          additionalHeaders: testHeaders);
      shelf.Response response = await requestHandler(request,
          moduleUploader: moduleUploader, authManager: authManager);
      expect(response.statusCode, HttpStatus.OK);

      Stream<List<int>> data =
          verify(moduleUploader.processUpload(captureAny)).captured.single;
      BytesBuilder bytesBuilder = await data.fold(
          new BytesBuilder(),
          (BytesBuilder bytesBuilder, List<int> bytes) =>
              bytesBuilder..add(bytes));
      expect(bytesBuilder.toBytes(), testBytes);
    });
  });
}
