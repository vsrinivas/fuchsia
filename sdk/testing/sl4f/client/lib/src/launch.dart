// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'sl4f_client.dart';

/// Launches components through sl4f
class Launch {
  final Sl4f _sl4f;

  Launch(this._sl4f);

  /// Launches components through sl4f, can be the component name or the full url of the componenet
  Future<dynamic> launch(String url, [List<String> args]) async {
    var packageUrl = url;
    if (!url.startsWith('fuchsia-pkg')) {
      packageUrl = 'fuchsia-pkg://fuchsia.com/$url#meta/$url.cmx';
    }
    if (args != null && args.isNotEmpty) {
      final result = await _sl4f.request(
          'launch_facade.Launch', {'url': packageUrl, 'arguments': args});
      return result;
    } else {
      final result =
          await _sl4f.request('launch_facade.Launch', {'url': packageUrl});
      return result;
    }
  }
}
