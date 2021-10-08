// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:ermine/src/services/settings/task_service.dart';
import 'package:fidl/fidl.dart' show InterfaceHandle, InterfaceRequest;
import 'package:fidl_fuchsia_wlan_common/fidl_async.dart';
import 'package:fidl_fuchsia_wlan_policy/fidl_async.dart';
import 'package:flutter/foundation.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:flutter/material.dart';
import 'package:fuchsia_services/services.dart';

/// Defines a [TaskService] for WiFi control.
class WiFiService implements TaskService {
  late final VoidCallback onChanged;

  ClientProviderProxy? _clientProvider;
  ClientControllerProxy? _clientController;
  late ClientStateUpdatesMonitor _monitor;
  StreamSubscription? _scanForNetworksSubscription;
  ScanResultIteratorProxy? _scanResultIteratorProvider;

  Timer? _timer;
  int scanIntervalInSeconds = 20;
  late List<ScanResult> _scannedNetworks;

  WiFiService();

  @override
  Future<void> start() async {
    _clientProvider = ClientProviderProxy();
    _clientController = ClientControllerProxy();
    _monitor = ClientStateUpdatesMonitor();

    Incoming.fromSvcPath().connectToService(_clientProvider);

    await _clientProvider?.getController(
        InterfaceRequest(_clientController?.ctrl.request().passChannel()),
        _monitor.getInterfaceHandle());

    final requestStatus = await _clientController?.startClientConnections();
    if (requestStatus != RequestStatus.acknowledged) {
      log.warning(
          'Failed to start wlan client connection. Request status: $requestStatus');
    }

    _timer = Timer.periodic(
        Duration(seconds: scanIntervalInSeconds), (_) => scanForNetworks());
  }

  @override
  Future<void> stop() async {
    _timer?.cancel();
    await _scanForNetworksSubscription?.cancel();
    dispose();
  }

  @override
  void dispose() {
    _clientProvider?.ctrl.close();
    _clientProvider = ClientProviderProxy();
    _clientController?.ctrl.close();
    _clientController = ClientControllerProxy();
    _scanResultIteratorProvider?.ctrl.close();
    _scanResultIteratorProvider = ScanResultIteratorProxy();
  }

  Future<void> scanForNetworks() async {
    _scanForNetworksSubscription = () async {
      _scanResultIteratorProvider = ScanResultIteratorProxy();
      await _clientController?.scanForNetworks(InterfaceRequest(
          _scanResultIteratorProvider?.ctrl.request().passChannel()));
      List<ScanResult> aggregateScanResults = [];
      List<ScanResult>? scanResults;
      try {
        scanResults = await _scanResultIteratorProvider?.getNext();
        while (scanResults != null && scanResults.isNotEmpty) {
          aggregateScanResults = aggregateScanResults + scanResults;
          scanResults = await _scanResultIteratorProvider?.getNext();
        }
      } on Exception catch (e) {
        log.warning('Error encountered during scan: $e');
      }
      _scannedNetworks = aggregateScanResults.toSet().toList();
    }()
        .asStream()
        .listen((_) {});
  }

  List<String?> get scannedNetworks =>
      _scannedNetworks.map((network) => network.id?.ssid.toString()).toList();
}

class ClientStateUpdatesMonitor extends ClientStateUpdates {
  final _binding = ClientStateUpdatesBinding();
  ClientStateSummary? _summary;

  ClientStateUpdatesMonitor();

  InterfaceHandle<ClientStateUpdates> getInterfaceHandle() =>
      _binding.wrap(this);

  ClientStateSummary? getState() => _summary;

  @override
  Future<void> onClientStateUpdate(ClientStateSummary summary) async {}
}
