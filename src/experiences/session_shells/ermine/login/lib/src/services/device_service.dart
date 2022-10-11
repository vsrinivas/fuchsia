// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_hardware_power_statecontrol/fidl_async.dart';
import 'package:fuchsia_inspect/inspect.dart';
import 'package:fuchsia_services/services.dart';

/// Defines a service to handle device-specific operations like shutdown and
/// factory data reset.
class DeviceService {
  /// Callback to service [Inspect] requests from the system.
  late final void Function(Node) onInspect;

  final _deviceManager = AdminProxy();
  final _inspect = Inspect();

  DeviceService() {
    Incoming.fromSvcPath().connectToService(_deviceManager);
  }

  void dispose() {
    _deviceManager.ctrl.close();
  }

  void serve(ComponentContext componentContext) {
    _inspect
      ..serve(componentContext.outgoing)
      ..onDemand('login', onInspect);
  }

  void shutdown() => _deviceManager.poweroff();
}
