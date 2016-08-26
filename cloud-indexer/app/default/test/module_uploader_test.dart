// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:cloud_indexer/module_uploader.dart';
import 'package:cloud_indexer/tarball.dart';
import 'package:gcloud/pubsub.dart';
import 'package:gcloud/storage.dart';
import 'package:googleapis/pubsub/v1.dart' show DetailedApiRequestError;
import 'package:mockito/mockito.dart';
import 'package:parser/manifest.dart';
import 'package:test/test.dart';

class MockTopic extends Mock implements Topic {}

class MockBucket extends Mock implements Bucket {}

class MockTarball implements Tarball {
  final Map<String, String> _files;

  MockTarball(this._files);

  Stream<String> list() => new Stream.fromIterable(_files.keys);

  Future<String> readAsString(String filename) {
    String file = _files[filename];
    if (file == null) throw new TarballException('File does not exist.');
    return new Future.value(file);
  }

  Stream<List<int>> openRead(String filename) {
    String file = _files[filename];
    if (file == null) throw new TarballException('File does not exist.');
    return new Stream.fromIterable([file.codeUnits]);
  }
}

class FakeSink<T> implements StreamSink<T> {
  final List<T> events = [];

  bool _closed = false;
  bool _addingStream = false;

  void _validateState() {
    if (_closed) throw new StateError('Cannot add to closed sink.');
    if (_addingStream) throw new StateError('Cannot write to busy stream.');
  }

  void add(T event) {
    _validateState();
    events.add(event);
  }

  void addError(errorEvent, [StackTrace stackTrace]) => _validateState();

  Future addStream(Stream<T> stream) async {
    _validateState();
    await for (T event in stream) {
      events.add(event);
    }
  }

  Future get done async => events;

  Future close() => done;
}

main() {
  group('processTarball', () {
    const String testArch = 'linux-x64';
    const String testModularRevision =
        'b54f77abb289dcf2e39bc6f78ecab189aaf77a89';

    const String manifestPath = 'manifest.yaml';
    const String manifestDestinationPath =
        'services/$testArch/$testModularRevision/$manifestPath';

    const String invalidManifest1 = 'title: Hello Fuchsia World';

    // Here, the modularRevision is too short.
    const String invalidManifest2 = 'title: Hello Fuchsia World\n'
        'arch: $testArch\n'
        'modularRevision: b54f77abb289dcf2e3bc6f78ecab189aaf77a89';

    const String validManifest = 'title: Hello Fuchsia World\n'
        'arch: $testArch\n'
        'modularRevision: $testModularRevision';

    const String otherPath = 'craft/README.md';
    const String otherPathContents = '# Hello World!';
    const String otherDestinationPath =
        'services/$testArch/$testModularRevision/$otherPath';

    const Map<String, String> validPayload = const {
      manifestPath: validManifest,
      otherPath: otherPathContents
    };

    final Matcher throwsTarballException =
        throwsA(new isInstanceOf<TarballException>());

    final Matcher throwsCloudStorageException =
        throwsA(new isInstanceOf<CloudStorageException>());

    final Matcher throwsPubSubException =
        throwsA(new isInstanceOf<PubSubException>());

    test('Missing manifest.', () {
      final Topic topic = new MockTopic();
      final Bucket bucket = new MockBucket();

      final Tarball tarball = new MockTarball({otherPath: otherPathContents});
      final ModuleUploader moduleUploader = new ModuleUploader(topic, bucket);

      expect(
          moduleUploader.processTarball(tarball).whenComplete(() {
            verifyNever(bucket.write(any));
            verifyNever(topic.publish(any));
          }),
          throwsTarballException);
    });

    test('Invalid manifests.', () {
      final Topic topic = new MockTopic();
      final Bucket bucket = new MockBucket();

      final Tarball tarball1 =
          new MockTarball({manifestPath: invalidManifest1});
      final Tarball tarball2 =
          new MockTarball({manifestPath: invalidManifest2});
      final ModuleUploader moduleUploader = new ModuleUploader(topic, bucket);

      expect(
          moduleUploader.processTarball(tarball1).whenComplete(() {
            verifyNever(bucket.write(any));
            verifyNever(topic.publish(any));
          }),
          throwsTarballException);

      expect(
          moduleUploader.processTarball(tarball2).whenComplete(() {
            verifyNever(bucket.write(any));
            verifyNever(topic.publish(any));
          }),
          throwsTarballException);
    });

    test('Cloud storage failure.', () {
      final Topic topic = new MockTopic();
      final Bucket bucket = new MockBucket();
      when(bucket.write(otherDestinationPath)).thenAnswer((i) =>
          throw new DetailedApiRequestError(
              HttpStatus.INTERNAL_SERVER_ERROR, 'Internal Server Error.'));

      final Tarball tarball = new MockTarball(validPayload);
      final ModuleUploader moduleUploader = new ModuleUploader(topic, bucket);

      expect(
          moduleUploader.processTarball(tarball).whenComplete(() {
            // The manifest should never be copied.
            verifyNever(bucket.write(manifestDestinationPath));
            verifyNever(topic.publish(any));
          }),
          throwsCloudStorageException);
    });

    test('Pub/Sub failure.', () {
      final Topic topic = new MockTopic();
      final Bucket bucket = new MockBucket();
      final StreamSink<List<int>> sink = new FakeSink<List<int>>();
      when(bucket.write(otherDestinationPath)).thenReturn(sink);
      when(topic.publish(any)).thenAnswer((i) =>
          throw new DetailedApiRequestError(
              HttpStatus.INTERNAL_SERVER_ERROR, 'Internal Server Error.'));

      final Tarball tarball = new MockTarball(validPayload);
      final ModuleUploader moduleUploader = new ModuleUploader(topic, bucket);

      expect(
          moduleUploader.processTarball(tarball).whenComplete(() async {
            verifyNever(bucket.write(manifestDestinationPath));
            List<List<int>> events = await sink.done;
            List<int> payload = events.fold(
                [], (List<int> data, List<int> bytes) => data..addAll(bytes));
            expect(payload, validPayload[otherPath].codeUnits);
          }),
          throwsPubSubException);
    });

    test('Valid request.', () async {
      final Topic topic = new MockTopic();
      final Bucket bucket = new MockBucket();
      final StreamSink<List<int>> sink = new FakeSink<List<int>>();
      when(bucket.write(otherDestinationPath)).thenReturn(sink);

      final Tarball tarball = new MockTarball(validPayload);
      final ModuleUploader moduleUploader = new ModuleUploader(topic, bucket);
      await moduleUploader.processTarball(tarball);

      List<List<int>> events = await sink.done;
      List<int> payload = events
          .fold([], (List<int> data, List<int> bytes) => data..addAll(bytes));
      expect(payload, validPayload[otherPath].codeUnits);

      Message message = verify(topic.publish(captureAny)).captured.single;
      Manifest manifest = new Manifest.parseYamlString(validManifest);
      expect(message.asBytes,
          new Message.withBytes(manifest.toJsonString().codeUnits).asBytes);
      verifyNever(bucket.write(manifestDestinationPath));
    });
  });
}
