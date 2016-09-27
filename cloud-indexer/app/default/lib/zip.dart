// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:logging/logging.dart';
import 'package:path/path.dart' as path;

final Logger _logger = new Logger('cloud_indexer.zip');

const String _zipName = 'module.zip';
const int _defaultMaxZipSize = 100 * 1024 * 1024;

class ZipException implements Exception {
  final String message;
  ZipException(this.message);
  String toString() => 'ZipException: $message';
}

ZipException _zipException(String message) {
  ZipException e = new ZipException(message);
  _logger.warning(e.toString());
  return e;
}

Stream<List<int>> cappedDataStream(
    Stream<List<int>> data, int maxZipSize) async* {
  int currentSize = 0;
  await for (List<int> bytes in data) {
    currentSize += bytes.length;
    if (currentSize > maxZipSize) {
      throw _zipException('Module exceeds the maximum upload size.');
    }
    yield bytes;
  }
}

Future withZip(Stream<List<int>> data, Future handler(Zip zip),
    {int maxZipSize: _defaultMaxZipSize}) async {
  Directory tempDirectory;
  try {
    tempDirectory = await Directory.systemTemp.createTemp();
    String zipPath = path.join(tempDirectory.absolute.path, _zipName);
    File zipFile = await new File(zipPath).create();

    // We cap the size of the data stream to prevent malicious uploads.
    await cappedDataStream(data, maxZipSize).pipe(zipFile.openWrite());
    return await handler(new FSZip(zipPath));
  } on IOException catch (e) {
    throw _zipException('Failed to create zip on FS: ${e.toString()}');
  } finally {
    await tempDirectory?.delete(recursive: true);
  }
}

/// Utility class representing the contents of a zip.
///
/// The method signatures attempt to replicate those in dart:io as closely as
/// possible. On failure, the methods throw [ZipException]s.
abstract class Zip {
  /// Lists the sub-directories and the files of this zip.
  Stream<String> list();

  /// Reads the file [filename] in the zip as a String.
  Future<String> readAsString(String filename);

  /// Create a new independent Stream for the contents of the file [filename].
  Stream<List<int>> openRead(String filename);
}

/// An implementation of [Zip] that interfaces with a zip on disk.
class FSZip implements Zip {
  final String _path;

  FSZip(this._path);

  Stream<String> list() async* {
    final Process process = await Process.start('unzip', ['-Z1', _path]);

    // Process.start requires that both stdout and stderr are fully consumed
    // before the associated system resources are released.
    process.stderr.drain();
    Stream<String> lines =
        process.stdout.transform(UTF8.decoder).transform(const LineSplitter());
    await for (String line in lines) {
      yield line;
    }

    if (await process.exitCode != 0) {
      throw _zipException('Failed to list contents of zip.');
    }
  }

  Future<String> readAsString(String filename) async {
    final ProcessResult result =
        await Process.run('unzip', ['-p', _path, filename]);
    if (result.exitCode != 0) {
      throw _zipException('Failed to read file: $filename in zip.');
    }
    return result.stdout;
  }

  Stream<List<int>> openRead(String filename) async* {
    final Process process =
        await Process.start('unzip', ['-p', _path, filename]);

    // Process.start requires that both stdout and stderr are fully consumed
    // before the associated system resources are released.
    process.stderr.drain();
    await for (List<int> bytes in process.stdout) {
      yield bytes;
    }

    if (await process.exitCode != 0) {
      throw _zipException('Failed to read file: $filename in zip.');
    }
  }
}
