// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io' show File;

import 'package:logging/logging.dart';

import 'sl4f_client.dart';

final _log = Logger('storage');

/// Interact with files in the DUT storage using [Sl4f].
class Storage {
  final Sl4f _sl4f;

  /// Constructs a [Storage] object.
  Storage(this._sl4f);

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
}
