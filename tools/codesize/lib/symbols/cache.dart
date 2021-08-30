// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

library symbols_cache;

import 'dart:async';
import 'dart:io';

import '../build.dart';

/// Caches symbols in a ".build-id" directory. The directory layout is
///
/// ```txt
/// .build-id/12/3456.debug
///    => for an ELF with a BuildId of "123456"
/// ```
///
/// The cache will place in-progress downloads in
///
/// ```txt
/// .build-id/123456.part
///    => for an ELF with a BuildId of "123456"
/// ```
///
/// before atomically moving it into `12/3456.debug` when finished.
class Cache {
  /// Creates a cache at the specified directory.
  /// The directory name is typically ".build-id".
  Cache(this.directory);

  /// If there is an entry with the specified BuildId in the cache,
  /// returns its absolute path. If not, returns `null`.
  Future<String> getEntry(String buildId) async {
    final file = pathForBuildId(buildId);
    if (file.existsSync()) {
      return file.absolute.path;
    }
    return null;
  }

  /// Downloads contents from the stream, then atomically adds it to the cache
  /// upon successs.
  Future<String> addEntry(String buildId, Stream<List<int>> contents) async {
    // Download to a temporary location first.
    final tempFile = await _tempFile(buildId);
    final sink = tempFile.file.openWrite();
    Object anyError;
    // Here and below we are being intentionally catch-all in exception
    // handling, to ensure that the temporary files always get cleaned up.
    try {
      await sink.addStream(contents);
      await sink.flush();
      // ignore: avoid_catches_without_on_clauses
    } catch (err) {
      anyError = err;
    }
    // Regardless of error, always close the stream
    try {
      await sink.close();
      // ignore: avoid_catches_without_on_clauses
    } catch (err) {
      // ignore: empty_catches
    }
    if (anyError != null) {
      try {
        await tempFile.delete();
        // ignore: avoid_catches_without_on_clauses
      } catch (err) {
        // ignore: empty_catches
      }
      // ignore: only_throw_errors
      throw anyError;
    }

    // Then atomically move to the cache folder.
    final file = pathForBuildId(buildId);
    await file.parent.create(recursive: true);
    await tempFile.move(file);
    return file.absolute.path;
  }

  File pathForBuildId(String buildId) {
    final prefix = buildId.substring(0, 2);
    final remaining = buildId.substring(2);
    // ignore: avoid_as
    return ((directory / Directory(prefix)) as Directory) /
        File('$remaining.debug');
  }

  File pathForTempBuildId(String buildId) => directory / File('$buildId.part');

  Future<TempFile> _tempFile(String buildId) async {
    final file = pathForTempBuildId(buildId);
    // Create parent directory.
    await file.parent.create(recursive: true);
    return TempFile(file);
  }

  Directory directory;
}

/// A temporary file.
class TempFile {
  TempFile(this.file);

  Future<void> move(File destination) async {
    await file.rename(destination.absolute.path);
  }

  Future<void> delete() async {
    await file.delete(recursive: true);
  }

  File file;
}
