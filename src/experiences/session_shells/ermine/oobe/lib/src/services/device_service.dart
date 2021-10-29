// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_device_manager/fidl_async.dart';
import 'package:fuchsia_services/services.dart';

/// Defines a service to handle device-specific operations like shutdown and
/// factory data reset.
class DeviceService {
  final _deviceManager = AdministratorProxy();

  DeviceService() {
    Incoming.fromSvcPath().connectToService(_deviceManager);
  }

  void dispose() {
    _deviceManager.ctrl.close();
  }

  void shutdown() => _deviceManager.suspend(suspendFlagPoweroff);
}
