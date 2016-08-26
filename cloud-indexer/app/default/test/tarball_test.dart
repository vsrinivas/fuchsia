// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';

import 'package:archive/archive.dart';
import 'package:cloud_indexer/tarball.dart';
import 'package:test/test.dart';

ArchiveFile createTestFile(String filename, List<int> data,
    {int mode,
    int ownerId: 322079,
    int groupId: 5762,
    int lastModTime: 1472275977,
    bool isFile: true}) {
  mode ??= int.parse('0600', radix: 8);

  ArchiveFile file = new ArchiveFile(filename, data.length, data);
  file.mode = mode;
  file.ownerId = ownerId;
  file.groupId = groupId;
  file.lastModTime = lastModTime;
  file.isFile = isFile;
  return file;
}

main() {
  final Matcher throwsTarballException =
      throwsA(new isInstanceOf<TarballException>());

  group('cappedDataStream', () {
    const List<int> testData = const [1, 2, 3, 4, 5];

    test('Oversized stream.', () {
      expect(cappedDataStream(new Stream.fromIterable([testData]), 1).drain(),
          throwsTarballException);
    });

    test('Regular stream.', () {
      cappedDataStream(new Stream.fromIterable([testData]), 16).fold(
          [],
          (List<int> data, List<int> bytes) =>
              data..addAll(bytes)).then((List<int> result) {
        expect(result, orderedEquals(testData));
      });
    });
  });

  group('tarball', () {
    // A directory, by convention, has a name that ends with a forward-slash.
    const Map<String, String> testFiles = const {
      'manifest.yaml': 'title: Adorable Cats\n'
          'arch: linux-x64\n'
          'modularRevision: b54f77abb289dcf2e3bc6f78ecab189aaf77a89',
      'hello/': '',
      'hello/world.txt': 'Hello world'
    };

    Archive testArchive = new Archive();
    testFiles.forEach((String key, String value) {
      testArchive.addFile(
          createTestFile(key, value.codeUnits, isFile: !key.endsWith('/')));
    });

    List<int> tarData = new TarEncoder().encode(testArchive);
    List<int> gzipData = new GZipEncoder().encode(tarData);
    Stream<List<int>> gzipDataStream() =>
        new Stream.fromFuture(new Future.value(gzipData));

    test('list', () {
      withTarball(gzipDataStream(), (Tarball tarball) async {
        expect(await tarball.list().toList(), unorderedEquals(testFiles.keys));
      });
    });

    test('readAsString', () {
      withTarball(gzipDataStream(), (Tarball tarball) async {
        expect(await tarball.readAsString('manifest.yaml'),
            testFiles['manifest.yaml']);
      });
    });

    test('readAsString, non-existent file.', () {
      withTarball(gzipDataStream(), (Tarball tarball) {
        expect(tarball.readAsString('manifest.json'), throwsTarballException);
      });
    });

    test('openRead', () {
      withTarball(gzipDataStream(), (Tarball tarball) async {
        expect(await UTF8.decodeStream(tarball.openRead('hello/world.txt')),
            testFiles['hello/world.txt']);
      });
    });

    test('openRead, non-existent file.', () {
      withTarball(gzipDataStream(), (Tarball tarball) {
        expect(UTF8.decodeStream(tarball.openRead('hello/world.md')),
            throwsTarballException);
      });
    });
  });
}
