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
import 'package:fidl_fuchsia_wlan_policy/fidl_async.dart' as policy;
import 'package:fuchsia_logger/logger.dart';
import 'package:flutter/material.dart';
import 'package:fuchsia_services/services.dart';

/// Defines a [TaskService] for WiFi control.
class WiFiService implements TaskService {
  late final VoidCallback onChanged;

  policy.ClientProviderProxy? _clientProvider;
  policy.ClientControllerProxy? _clientController;
  late ClientStateUpdatesMonitor _monitor;
  StreamSubscription? _scanForNetworksSubscription;
  policy.ScanResultIteratorProxy? _scanResultIteratorProvider;
  StreamSubscription? _connectToWPA2NetworkSubscription;
  StreamSubscription? _savedNetworksSubscription;
  StreamSubscription? _removeNetworkSubscription;

  Timer? _timer;
  int scanIntervalInSeconds = 20;
  final _scannedNetworks = <policy.ScanResult>{};
  String _targetNetwork = '';
  final _savedNetworks = <policy.NetworkConfig>{};

  WiFiService();

  @override
  Future<void> start() async {
    _clientProvider = policy.ClientProviderProxy();
    _clientController = policy.ClientControllerProxy();
    _monitor = ClientStateUpdatesMonitor(onChanged);

    Incoming.fromSvcPath().connectToService(_clientProvider);

    await _clientProvider?.getController(
        InterfaceRequest(_clientController?.ctrl.request().passChannel()),
        _monitor.getInterfaceHandle());

    final requestStatus = await _clientController?.startClientConnections();
    if (requestStatus != RequestStatus.acknowledged) {
      log.warning(
          'Failed to start wlan client connection. Request status: $requestStatus');
    }

    await getSavedNetworks();

    _timer = Timer.periodic(
        Duration(seconds: scanIntervalInSeconds), (_) => scanForNetworks());
  }

  @override
  Future<void> stop() async {
    _timer?.cancel();
    await _scanForNetworksSubscription?.cancel();
    await _connectToWPA2NetworkSubscription?.cancel();
    await _savedNetworksSubscription?.cancel();
    await _removeNetworkSubscription?.cancel();
    dispose();
  }

  @override
  void dispose() {
    _clientProvider?.ctrl.close();
    _clientProvider = policy.ClientProviderProxy();
    _clientController?.ctrl.close();
    _clientController = policy.ClientControllerProxy();
    _scanResultIteratorProvider?.ctrl.close();
    _scanResultIteratorProvider = policy.ScanResultIteratorProxy();
  }

  String get targetNetwork => _targetNetwork;
  set targetNetwork(String network) {
    _targetNetwork = network;
    onChanged();
  }

  Future<void> scanForNetworks() async {
    _scanForNetworksSubscription = () async {
      _scanResultIteratorProvider = policy.ScanResultIteratorProxy();
      await _clientController?.scanForNetworks(InterfaceRequest(
          _scanResultIteratorProvider?.ctrl.request().passChannel()));

      _scannedNetworks.clear();
      List<policy.ScanResult>? scanResults;
      try {
        scanResults = await _scanResultIteratorProvider?.getNext();
        while (scanResults != null && scanResults.isNotEmpty) {
          _scannedNetworks.addAll(scanResults);
          scanResults = await _scanResultIteratorProvider?.getNext();
        }
      } on Exception catch (e) {
        log.warning('Error encountered during scan: $e');
        return;
      }
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

  String nameFromScannedNetwork(policy.ScanResult network) {
    return utf8.decode(network.id!.ssid.toList());
  }

  IconData iconFromScannedNetwork(policy.ScanResult network) {
    return network.id!.type == policy.SecurityType.none
        ? Icons.signal_wifi_4_bar
        : Icons.wifi_lock;
  }

  bool compatibleFromScannedNetwork(policy.ScanResult network) {
    return network.compatibility == policy.Compatibility.supported;
  }

  Future<void> connectToWPA2Network(String password) async {
    try {
      _connectToWPA2NetworkSubscription = () async {
        final utf8password = Uint8List.fromList(password.codeUnits);
        final credential = policy.Credential.withPassword(utf8password);
        policy.ScanResult? network = _scannedNetworks.firstWhereOrNull(
            (network) => nameFromScannedNetwork(network) == _targetNetwork);

        if (network == null) {
          throw Exception(
              '$targetNetwork network not found in scanned networks.');
        }

        final networkConfig =
            policy.NetworkConfig(id: network.id, credential: credential);

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

  String? get currentNetwork => _monitor.currentNetwork();

  bool get connectionsEnabled => _monitor.connectionsEnabled();

  bool get incorrectPassword => _monitor.incorrectPassword();

  Future<void> getSavedNetworks() async {
    _savedNetworksSubscription = () async {
      final iterator = policy.NetworkConfigIteratorProxy();
      await _clientController?.getSavedNetworks(
          InterfaceRequest(iterator.ctrl.request().passChannel()));

      _savedNetworks.clear();
      var savedNetworkResults = await iterator.getNext();
      while (savedNetworkResults.isNotEmpty) {
        _savedNetworks.addAll(savedNetworkResults);
        savedNetworkResults = await iterator.getNext();
      }
      onChanged();
    }()
        .asStream()
        .listen((_) {});
  }

  // TODO(fxb/79885): Pass security type to ensure removing correct network
  Future<void> remove(String network) async {
    try {
      _removeNetworkSubscription = () async {
        final ssid = utf8.encode(network);
        final foundNetwork = _savedNetworks
            .firstWhereOrNull((network) => network.id?.ssid == ssid);

        if (foundNetwork == null) {
          throw Exception('$network not found in saved networks.');
        }

        final networkConfig = policy.NetworkConfig(
            id: foundNetwork.id, credential: foundNetwork.credential);

        await _clientController?.removeNetwork(networkConfig);
      }()
          .asStream()
          .listen((_) {});
    } on Exception catch (e) {
      log.warning('Removing $network failed: $e');
    }
  }
}

class ClientStateUpdatesMonitor extends policy.ClientStateUpdates {
  final _binding = policy.ClientStateUpdatesBinding();
  policy.ClientStateSummary? _summary;
  late final VoidCallback _onChanged;

  ClientStateUpdatesMonitor(this._onChanged);

  InterfaceHandle<policy.ClientStateUpdates> getInterfaceHandle() =>
      _binding.wrap(this);

  policy.ClientStateSummary? getState() => _summary;

  bool connectionsEnabled() =>
      _summary?.state == policy.WlanClientState.connectionsEnabled;

  // Returns first found connected network.
  // TODO(fxb/79885): expand to return multiple connected networks.
  String? currentNetwork() {
    final foundNetwork = _summary?.networks
        ?.firstWhereOrNull(
            (network) => network.state == policy.ConnectionState.connected)
        ?.id!
        .ssid
        .toList();
    return foundNetwork == null ? null : utf8.decode(foundNetwork);
  }

  // TODO(fxb/79855): ensure that failed password status is for target network
  bool incorrectPassword() {
    return _summary?.networks?.firstWhereOrNull((network) =>
            network.status == policy.DisconnectStatus.credentialsFailed) !=
        null;
  }

  @override
  Future<void> onClientStateUpdate(policy.ClientStateSummary summary) async {
    _summary = summary;
    _onChanged();
  }
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
