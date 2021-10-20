// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:collection/collection.dart';
import 'package:ermine/src/services/settings/task_service.dart';
import 'package:fidl/fidl.dart' show InterfaceHandle, InterfaceRequest;
import 'package:fidl_fuchsia_wlan_common/fidl_async.dart';
import 'package:fidl_fuchsia_wlan_policy/fidl_async.dart';
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
  StreamSubscription? _connectToWPA2NetworkSubscription;

  Timer? _timer;
  int scanIntervalInSeconds = 20;
  late List<ScanResult> _scannedNetworks;
  String _targetNetwork = '';

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
    await _connectToWPA2NetworkSubscription?.cancel();
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

  String get targetNetwork => _targetNetwork;
  set targetNetwork(String network) {
    _targetNetwork = network;
    onChanged();
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
      onChanged();
    }()
        .asStream()
        .listen((_) {});
  }

  List<NetworkInformation> get scannedNetworks => _scannedNetworks
      .map((network) => NetworkInformation(
          name: nameFromScannedNetwork(network),
          compatible: compatibleFromScannedNetwork(network),
          icon: iconFromScannedNetwork(network)))
      .toList();

  String nameFromScannedNetwork(ScanResult network) {
    return utf8.decode(network.id!.ssid.toList());
  }

  IconData iconFromScannedNetwork(ScanResult network) {
    return network.id!.type == SecurityType.none
        ? Icons.signal_wifi_4_bar
        : Icons.wifi_lock;
  }

  bool compatibleFromScannedNetwork(ScanResult network) {
    return network.compatibility == Compatibility.supported;
  }

  Future<void> connectToWPA2Network(String password) async {
    try {
      _connectToWPA2NetworkSubscription = () async {
        final utf8password = Uint8List.fromList(password.codeUnits);
        final credential = Credential.withPassword(utf8password);
        ScanResult? network = _scannedNetworks.firstWhereOrNull(
            (network) => nameFromScannedNetwork(network) == _targetNetwork);

        if (network == null) {
          throw Exception(
              '$targetNetwork network not found in scanned networks.');
        }

        final networkConfig =
            NetworkConfig(id: network.id, credential: credential);

        // TODO(fxb/79885): Separate save and connect functionality.
        await _clientController?.saveNetwork(networkConfig);

        final requestStatus = await _clientController?.connect(network.id!);
        if (requestStatus != RequestStatus.acknowledged) {
          throw Exception(
              'connecting to $targetNetwork rejected: $requestStatus.');
        }
      }()
          .asStream()
          .listen((_) {});
    } on Exception catch (e) {
      log.warning('Connecting to $targetNetwork failed: $e');
    }
  }
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

/// Network information needed for UI
class NetworkInformation {
  String name;
  bool compatible;
  IconData icon;

  NetworkInformation(
      {this.name = '',
      this.compatible = false,
      this.icon = Icons.signal_wifi_4_bar});
}
