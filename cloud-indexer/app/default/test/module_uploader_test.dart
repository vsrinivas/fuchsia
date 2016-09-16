// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:cloud_indexer/module_uploader.dart';
import 'package:cloud_indexer/zip.dart';
import 'package:cloud_indexer_common/wrappers.dart';
import 'package:mockito/mockito.dart';
import 'package:parser/manifest.dart';
import 'package:test/test.dart';

class MockTopicWrapper extends Mock implements PubSubTopicWrapper {}

class MockBucketWrapper extends Mock implements StorageBucketWrapper {}

class MockZip implements Zip {
  final Map<String, String> _files;

  MockZip(this._files);

  Stream<String> list() => new Stream.fromIterable(_files.keys);

  Future<String> readAsString(String filename) {
    String file = _files[filename];
    if (file == null) throw new ZipException('File does not exist.');
    return new Future.value(file);
  }

  Stream<List<int>> openRead(String filename) {
    String file = _files[filename];
    if (file == null) throw new ZipException('File does not exist.');
    return new Stream.fromIterable([file.codeUnits]);
  }
}

main() {
  group('processZip', () {
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

    final Matcher isZipException = new isInstanceOf<ZipException>();

    final Matcher isCloudStorageException =
        new isInstanceOf<CloudStorageException>();

    final Matcher isPubSubException = new isInstanceOf<PubSubException>();

    test('Missing manifest.', () async {
      final PubSubTopicWrapper topic = new MockTopicWrapper();
      final StorageBucketWrapper bucket = new MockBucketWrapper();

      final Zip zip = new MockZip({otherPath: otherPathContents});
      final ModuleUploader moduleUploader = new ModuleUploader(topic, bucket);

      try {
        await moduleUploader.processZip(zip);
      } catch (e) {
        expect(e, isZipException);
      } finally {
        verifyNever(bucket.writeObjectAsBytes(any, any));
        verifyNever(bucket.writeObject(any));
        verifyNever(topic.publish(any));
      }
    });

    test('Invalid manifests.', () async {
      final PubSubTopicWrapper topic = new MockTopicWrapper();
      final StorageBucketWrapper bucket = new MockBucketWrapper();

      final Zip zip1 =
          new MockZip({manifestPath: invalidManifest1});
      final Zip zip2 =
          new MockZip({manifestPath: invalidManifest2});
      final ModuleUploader moduleUploader = new ModuleUploader(topic, bucket);

      try {
        await moduleUploader.processZip(zip1);
      } catch (e) {
        expect(e, isZipException);
      } finally {
        verifyNever(bucket.writeObjectAsBytes(any, any));
        verifyNever(bucket.writeObject(any));
        verifyNever(topic.publish(any));
      }

      try {
        await moduleUploader.processZip(zip2);
      } catch (e) {
        expect(e, isZipException);
      } finally {
        verifyNever(bucket.writeObjectAsBytes(any, any));
        verifyNever(bucket.writeObject(any));
        verifyNever(topic.publish(any));
      }
    });

    test('Cloud storage failure.', () async {
      final PubSubTopicWrapper topic = new MockTopicWrapper();
      final StorageBucketWrapper bucket = new MockBucketWrapper();

      when(bucket.writeObject(otherDestinationPath)).thenAnswer((i) =>
          throw new DetailedApiRequestError(
              HttpStatus.INTERNAL_SERVER_ERROR, 'Internal Server Error.'));

      final Zip zip = new MockZip(validPayload);
      final ModuleUploader moduleUploader = new ModuleUploader(topic, bucket);

      try {
        await moduleUploader.processZip(zip);
      } catch (e) {
        expect(e, isCloudStorageException);
      } finally {
        // The manifest should never be copied.
        verifyNever(bucket.writeObjectAsBytes(manifestDestinationPath, any));
        verifyNever(bucket.writeObject(manifestDestinationPath));
        verifyNever(topic.publish(any));
      }
    });

    test('Pub/Sub failure.', () async {
      final PubSubTopicWrapper topic = new MockTopicWrapper();
      final StorageBucketWrapper bucket = new MockBucketWrapper();
      final StreamController<List<int>> sink =
          new StreamController<List<int>>();
      when(bucket.writeObject(otherDestinationPath)).thenReturn(sink);
      when(topic.publish(any)).thenAnswer((i) => throw new PubSubException(
          HttpStatus.INTERNAL_SERVER_ERROR, 'Internal server error.'));

      final Zip zip = new MockZip(validPayload);
      final ModuleUploader moduleUploader = new ModuleUploader(topic, bucket);

      try {
        await moduleUploader.processZip(zip);
      } catch (e) {
        expect(e, isPubSubException);
      } finally {
        verifyNever(bucket.writeObject(manifestDestinationPath));
        List<int> payload = await sink.stream
            .fold([], (List<int> data, List<int> bytes) => data..addAll(bytes));
        expect(payload, validPayload[otherPath].codeUnits);
      }
    });

    test('Valid request.', () async {
      final PubSubTopicWrapper topic = new MockTopicWrapper();
      final StorageBucketWrapper bucket = new MockBucketWrapper();
      final StreamController<List<int>> sink =
          new StreamController<List<int>>();
      when(bucket.writeObject(otherDestinationPath)).thenReturn(sink);

      final Zip zip = new MockZip(validPayload);
      final ModuleUploader moduleUploader = new ModuleUploader(topic, bucket);
      await moduleUploader.processZip(zip);

      List<int> payload = await sink.stream
          .fold([], (List<int> data, List<int> bytes) => data..addAll(bytes));
      expect(payload, validPayload[otherPath].codeUnits);

      String data = verify(topic.publish(captureAny)).captured.single;
      Manifest manifest = new Manifest.parseYamlString(validManifest);
      expect(data, manifest.toJsonString());
      verifyNever(bucket.writeObject(manifestDestinationPath));
    });
  });
}
