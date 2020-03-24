// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'sl4f_client.dart';

/// Allows controlling a Modular session and its components.
class Launch {
  final Sl4f _sl4f;

  Launch(this._sl4f);

  Future<String> launch(String url, [List<String> args]) async {
    if (args != null && args.isNotEmpty) {
      return await _sl4f
          .request('launch_facade.Launch', {'url': url, 'arguments': args});
    } else {
      return await _sl4f.request('launch_facade.Launch', {'url': url});
    }
  }
}
