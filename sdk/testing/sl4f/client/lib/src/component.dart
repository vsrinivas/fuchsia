// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'sl4f_client.dart';

/// Allows controlling a Modular session and its components.
class Component {
  final Sl4f _sl4f;

  Component(this._sl4f);

  Future<List<String>> list() async {
    final result = await _sl4f.request('component_facade.List');
    return result.cast<String>();
  }

  Future<bool> search(String name) async {
    final result =
        await _sl4f.request('component_facade.Search', {'name': name});
    return result == 'Success';
  }

  Future<dynamic> launch(String url, [List<String> args]) async {
    var packageUrl = url;
    if (!url.startsWith('fuchsia-pkg')) {
      packageUrl = 'fuchsia-pkg://fuchsia.com/$url#meta/$url.cmx';
    }
    if (args != null && args.isNotEmpty) {
      return await _sl4f.request(
          'component_facade.Launch', {'url': packageUrl, 'arguments': args});
    }
    return await _sl4f.request('component_facade.Launch', {'url': packageUrl});
  }
}
