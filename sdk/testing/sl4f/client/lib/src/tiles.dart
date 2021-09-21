// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:logging/logging.dart';

import 'sl4f_client.dart';

class TilesListResponse {
  final String url;
  final int key;
  final bool focusable;

  TilesListResponse(this.url, this.key, {this.focusable});
}

final _log = Logger('tiles_sl4f');

class Tiles {
  final Sl4f _sl4f;

  Tiles(this._sl4f);

  Future<void> start() => _sl4f.request('tiles_facade.Start', null);

  Future<void> stop() => _sl4f.request('tiles_facade.Stop', null);

  Future<List<TilesListResponse>> list() async {
    final result = await _sl4f.request('tiles_facade.List', null);
    _log.info('Found tiles list: ${result.toString()}');
    final List<TilesListResponse> resp = <TilesListResponse>[];
    if (result != null) {
      final List<dynamic> keys = result['keys'];
      final List<dynamic> urls = result['urls'];
      final List<dynamic> focuses = result['focus'];

      for (var i = 0; i < keys.length; i++) {
        resp.add(TilesListResponse(urls[i], keys[i], focusable: focuses[i]));
      }
    }
    return resp;
  }

  Future<void> remove(int key) async {
    await _sl4f.request('tiles_facade.Remove', {'key': key.toString()});
  }

  Future<int> addFromUrl(String url,
      {bool allowFocus, List<String> args}) async {
    final Map<String, dynamic> request = {'url': url};
    if (allowFocus != null) {
      request['allowed_focus'] = allowFocus;
    }
    if (args != null) {
      request['args'] = args;
    }
    final result = await _sl4f.request('tiles_facade.AddFromUrl', request);
    if (result != null) {
      return result['key'];
    }
    return null;
  }
}
