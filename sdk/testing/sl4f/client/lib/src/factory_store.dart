// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:typed_data' show Uint8List;

import 'sl4f_client.dart';

/// Provider for a factory store that exposes a set of files.
enum FactoryStoreProvider {
  cast,
  misc,
  playready,
  weave,
  widevine,
}

final _providerToJson = {
  FactoryStoreProvider.cast: 'cast',
  FactoryStoreProvider.misc: 'misc',
  FactoryStoreProvider.playready: 'playready',
  FactoryStoreProvider.weave: 'weave',
  FactoryStoreProvider.widevine: 'widevine',
};

/// Allows listing and reading data from factory stores.
class FactoryStore {
  final Sl4f _sl4f;

  FactoryStore(this._sl4f);

  /// Lists FactoryStore files in the given [provider].
  Future<List<String>> listFiles(FactoryStoreProvider provider) async {
    final fileList = await _sl4f.request('factory_store_facade.ListFiles',
        {'provider': _providerToJson[provider]});
    return fileList.cast<String>();
  }

  /// Reads [filename] from the given [provider].
  ///
  /// Returns list of bytes of the file contents.
  /// If [filename] is not found, a JsonRpcException is thrown.
  Future<Uint8List> readFile(
      FactoryStoreProvider provider, String filename) async {
    final file = await _sl4f.request('factory_store_facade.ReadFile', {
      'filename': filename,
      'provider': _providerToJson[provider],
    });
    return base64Decode(file);
  }
}
