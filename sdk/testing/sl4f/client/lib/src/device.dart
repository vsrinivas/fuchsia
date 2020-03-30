// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'sl4f_client.dart';

/// Allows controlling a Modular session and its components.
class Device {
  final Sl4f _sl4f;

  Device(this._sl4f);

  /// Returns target's nodename
  Future<String> getDeviceName() async =>
      await _sl4f.request('device_facade.GetDeviceName');
}
