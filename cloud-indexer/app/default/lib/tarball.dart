// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:logging/logging.dart';
import 'package:path/path.dart' as path;

final Logger _logger = new Logger('cloud_indexer.tarball');

const String _tarballName = 'module.tar.gz';
const int _defaultMaxTarballSize = 100 * 1024 * 1024;

class TarballException implements Exception {
  final String message;
  TarballException(this.message);
  String toString() => 'TarballException: $message';
}

TarballException _tarballException(String message) {
  TarballException e = new TarballException(message);
  _logger.warning(e.toString());
  return e;
}

Stream<List<int>> cappedDataStream(
    Stream<List<int>> data, int maxTarballSize) async* {
  int currentSize = 0;
  await for (List<int> bytes in data) {
    currentSize += bytes.length;
    if (currentSize > maxTarballSize) {
      throw _tarballException('Module exceeds the maximum upload size.');
    }
    yield bytes;
  }
}

Future withTarball(Stream<List<int>> data, Future handler(Tarball tarball),
    {int maxTarballSize: _defaultMaxTarballSize}) async {
  Directory tempDirectory;
  try {
    tempDirectory = await Directory.systemTemp.createTemp();
    String tarballPath = path.join(tempDirectory.absolute.path, _tarballName);
    File tarballFile = await new File(tarballPath).create();

    // We cap the size of the data stream to prevent malicious uploads.
    await cappedDataStream(data, maxTarballSize).pipe(tarballFile.openWrite());
    return await handler(new FSTarball(tarballPath));
  } on IOException catch (e) {
    throw _tarballException('Failed to create tarball on FS: ${e.toString()}');
  } finally {
    await tempDirectory?.delete(recursive: true);
  }
}

/// Utility class representing the contents of a tarball.
///
/// The method signatures attempt to replicate those in dart:io as closely as
/// possible. On failure, the methods throw [TarballException]s.
abstract class Tarball {
  /// Lists the sub-directories and the files of this tarball.
  Stream<String> list();

  /// Reads the file [filename] in the tarball as a String.
  Future<String> readAsString(String filename);

  /// Create a new independent Stream for the contents of the file [filename].
  Stream<List<int>> openRead(String filename);
}

/// An implementation of [Tarball] that interfaces with a tarball on disk.
class FSTarball implements Tarball {
  final String _path;

  FSTarball(this._path);

  Stream<String> list() async* {
    final Process process = await Process.start('tar', ['-tzf', _path]);

    // Process.start requires that both stdout and stderr are fully consumed
    // before the associated system resources are released.
    process.stderr.drain();
    Stream<String> lines =
        process.stdout.transform(UTF8.decoder).transform(const LineSplitter());
    await for (String line in lines) {
      yield line;
    }

    if (await process.exitCode != 0) {
      throw _tarballException('Failed to list contents of tarball.');
    }
  }

  Future<String> readAsString(String filename) async {
    final ProcessResult result =
        await Process.run('tar', ['-Oxzf', _path, filename]);
    if (result.exitCode != 0) {
      throw _tarballException('Failed to read file: $filename in tarball.');
    }
    return result.stdout;
  }

  Stream<List<int>> openRead(String filename) async* {
    final Process process =
        await Process.start('tar', ['-Oxzf', _path, filename]);

    // Process.start requires that both stdout and stderr are fully consumed
    // before the associated system resources are released.
    process.stderr.drain();
    await for (List<int> bytes in process.stdout) {
      yield bytes;
    }

    if (await process.exitCode != 0) {
      throw _tarballException('Failed to read file: $filename in tarball.');
    }
  }
}
