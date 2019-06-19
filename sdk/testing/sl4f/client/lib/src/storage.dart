// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io' show File;

import 'package:logging/logging.dart';

import 'dump.dart';
import 'sl4f_client.dart';

final _log = Logger('storage');

/// Interact with files in the DUT storage using [Sl4f].
class Storage {
  final Sl4f _sl4f;
  final Dump _dump;

  /// Constructs a [Storage] object.
  Storage(this._sl4f, [Dump dump]) : _dump = dump ?? Dump();

  /// Closes the underlying HTTP client. This need not be called if the
  /// Sl4f client was provided.
  void close() {
    _sl4f.close();
  }

  /// Puts the contents of [file] in [filename] in the DUT.
  ///
  /// Throws a [JsonRpcException] if the SL4F server replied with a non-null
  /// error string.
  Future<void> putFile(String filename, File file) async {
    final bytes = await file.readAsBytes();
    return putBytes(filename, bytes);
  }

  /// Puts [bytes] in [filename] in the DUT.
  ///
  /// Throws a [JsonRpcException] if the SL4F server replied with a non-null
  /// error string.
  Future<void> putBytes(String filename, List<int> bytes) {
    _log.fine('Creating $filename (${bytes.length} bytes).');
    return _sl4f.request('file_facade.WriteFile', {
      'dst': filename,
      'data': base64Encode(bytes),
    });
  }

  /// Reads the contents of file.
  ///
  /// Throws a [JsonRpcException] if the SL4F server replied with a non-null
  /// error string, like when the file doesn't exist or can't be read.
  Future<List<int>> readFile(String filename) async {
    _log.fine('Reading $filename.');
    final result =
        await _sl4f.request('file_facade.ReadFile', {'path': filename});
    return base64.decode(result);
  }

  /// Dumps the contents of a file to a local file.
  ///
  /// Throws a [JsonRpcException] if the SL4F server replied with a non-null
  /// error string, like when the file doesn't exist or can't be read.
  Future<File> dumpFile(
          String filename, String dumpName, String extension) async =>
      _dump.writeAsBytes(dumpName, extension, await readFile(filename));

  /// Deletes a file in the device under test.
  ///
  /// Throws a [JsonRpcException] if the SL4F server replied with a non-null
  /// error string. It returns "NotFound" when the file didn't exist to begin
  /// with.
  Future<String> deleteFile(String filename) async {
    _log.fine('Deleting $filename.');
    String result =
        await _sl4f.request('file_facade.DeleteFile', {'path': filename});
    return result;
  }

  /// Creates a new directory.
  ///
  /// If [recurse] is true, then all parent directories are also created.
  /// Throws a [JsonRpcException] if the SL4F server replied with a non-null
  /// error string. It returns "AlreadyExists" when the directory already
  /// exists.
  Future<String> makeDirectory(String path, {bool recurse = false}) async {
    _log.fine('Creating directory $path.');
    String result = await _sl4f
        .request('file_facade.MakeDir', {'path': path, 'recurse': recurse});
    return result;
  }
}
