// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'sl4f_client.dart';

/// Allows controlling a Modular session and its components.
class Modular {
  final Sl4f _sl4f;

  Modular(this._sl4f);

  /// Restarts a Modular session.
  ///
  /// This is equivalent to sessionctl restart_session.
  Future<String> restartSession() async =>
      await _sl4f.request('basemgr_facade.RestartSession');

  /// Kill Basemgr.
  ///
  /// This is equivalent to sessionctl shutdown_basemgr.
  Future<String> killBasemgr() async =>
      await _sl4f.request('basemgr_facade.KillBasemgr');

  /// Launches Basemgr.
  ///
  /// Take custom config (in json) if there's one,
  /// or launch basemgr with defualt config.
  Future<String> startBasemgr([String config]) async {
    if (config != null && config.isNotEmpty) {
      return await _sl4f
          .request('basemgr_facade.StartBasemgr', {'config': config});
    } else {
      return await _sl4f.request('basemgr_facade.StartBasemgr', {});
    }
  }
}
