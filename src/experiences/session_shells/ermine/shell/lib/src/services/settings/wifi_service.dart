// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:ermine/src/services/settings/task_service.dart';
import 'package:fidl/fidl.dart' show InterfaceHandle, InterfaceRequest;
import 'package:fidl_fuchsia_wlan_policy/fidl_async.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:fuchsia_services/services.dart';

/// Defines a [TaskService] for WiFi control.
class WiFiService implements TaskService {
  late final VoidCallback onChanged;

  ClientProviderProxy? _clientProvider;
  ClientControllerProxy? _clientController;
  late ClientStateUpdatesMonitor _monitor;

  WiFiService();

  @override
  Future<void> start() async {
    _clientProvider = ClientProviderProxy();
    _clientController = ClientControllerProxy();
    _monitor = ClientStateUpdatesMonitor();

    Incoming.fromSvcPath().connectToService(_clientProvider);
    Incoming.fromSvcPath().connectToService(_clientController);

    await _clientProvider?.getController(
        InterfaceRequest(_clientController?.ctrl.request().passChannel()),
        _monitor.getInterfaceHandle());
  }

  @override
  Future<void> stop() async {
    dispose();
  }

  @override
  void dispose() {
    _clientProvider?.ctrl.close();
    _clientProvider = ClientProviderProxy();
    _clientController?.ctrl.close();
    _clientController = ClientControllerProxy();
  }
}

class ClientStateUpdatesMonitor extends ClientStateUpdates {
  final _binding = ClientStateUpdatesBinding();

  ClientStateUpdatesMonitor();

  InterfaceHandle<ClientStateUpdates> getInterfaceHandle() =>
      _binding.wrap(this);

  @override
  Future<void> onClientStateUpdate(ClientStateSummary summary) async {}
}
