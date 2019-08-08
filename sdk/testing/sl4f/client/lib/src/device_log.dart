// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
import 'sl4f_client.dart';

/// Manipulate the device's logs.
///
/// For now it can only write to the device's logs.
class DeviceLog {
  final Sl4f _sl4f;

  DeviceLog(this._sl4f);

  /// Writes to the logs of the device at Error severity.
  Future<void> error(String message) =>
      _sl4f.request('logging_facade.LogErr', {'message': message});

  /// Writes to the logs of the device at Info severity.
  Future<void> info(String message) =>
      _sl4f.request('logging_facade.LogInfo', {'message': message});

  /// Writes to the logs of the device at Warning severity.
  Future<void> warn(String message) =>
      _sl4f.request('logging_facade.LogWarn', {'message': message});
}
