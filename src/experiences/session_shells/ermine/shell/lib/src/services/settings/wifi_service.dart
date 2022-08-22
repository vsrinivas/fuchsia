// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';

import 'package:collection/collection.dart';
import 'package:ermine/src/services/settings/task_service.dart';
import 'package:fidl/fidl.dart' show InterfaceHandle, InterfaceRequest;
import 'package:fidl_fuchsia_wlan_common/fidl_async.dart';
import 'package:fidl_fuchsia_wlan_policy/fidl_async.dart' as policy;
import 'package:fuchsia_logger/logger.dart';
import 'package:flutter/foundation.dart';
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
  StreamSubscription? _connectToNetworkSubscription;
  StreamSubscription? _savedNetworksSubscription;
  StreamSubscription? _removeNetworkSubscription;
  StreamSubscription? _startClientConnectionsSubscription;
  StreamSubscription? _stopClientConnectionsSubscription;

  Timer? _scanTimer;
  int scanIntervalInSeconds = 20;
  final _scannedNetworks = <policy.ScanResult>{};
  NetworkInformation _targetNetwork = NetworkInformation();
  final _savedNetworks = <policy.NetworkConfig>{};
  bool _clientConnectionsEnabled = false;
  final _networksWithFailedCredentials = <policy.NetworkConfig>{};
  Stopwatch? _toggleTimer;
  Timer? _toggleTimerObserver;

  WiFiService();

  @override
  Future<void> start() async {
    _clientProvider = policy.ClientProviderProxy();
    _clientController = policy.ClientControllerProxy();
    _monitor = ClientStateUpdatesMonitor(
        onChanged, _pollNetworksWithFailedCredentials);

    Incoming.fromSvcPath().connectToService(_clientProvider);

    await _clientProvider?.getController(
        InterfaceRequest(_clientController?.ctrl.request().passChannel()),
        _monitor.getInterfaceHandle());

    clientConnectionsEnabled = true;

    await getSavedNetworks();

    _scanTimer = Timer.periodic(
        Duration(seconds: scanIntervalInSeconds), (_) => scanForNetworks());

    _toggleTimer = Stopwatch();
    _toggleTimerObserver = Timer.periodic(
        Duration(milliseconds: 100), (_) => observeToggleTimer());
  }

  @override
  Future<void> stop() async {
    _scanTimer?.cancel();
    await _scanForNetworksSubscription?.cancel();
    await _connectToNetworkSubscription?.cancel();
    await _savedNetworksSubscription?.cancel();
    await _removeNetworkSubscription?.cancel();
    await _startClientConnectionsSubscription?.cancel();
    await _stopClientConnectionsSubscription?.cancel();
    _toggleTimer?.stop();
    _toggleTimerObserver?.cancel();
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

  NetworkInformation get targetNetwork => _targetNetwork;
  set targetNetwork(NetworkInformation network) {
    _targetNetwork = network;
    onChanged();
  }

  bool get clientConnectionsEnabled => _clientConnectionsEnabled;
  set clientConnectionsEnabled(bool enabled) {
    _clientConnectionsEnabled = enabled;
    if (enabled) {
      _startClientConnections();
    } else {
      _stopClientConnections();
    }
    _toggleTimer?.reset();
    _toggleTimer?.start();
    _toggleTimerObserver = Timer.periodic(
        Duration(milliseconds: 100), (_) => observeToggleTimer());
    onChanged();
  }

  void observeToggleTimer() async {
    // Trigger onChanged when timer is between 0 and 6 seconds,
    // otherwise stop observing until toggle called again.
    if (toggleMillisecondsPassed >= 0 && toggleMillisecondsPassed < 6000) {
      onChanged();
    } else {
      _toggleTimerObserver?.cancel();
    }
  }

  int get toggleMillisecondsPassed => _toggleTimer?.elapsedMilliseconds ?? -1;

  Future<void> _startClientConnections() async {
    if (_stopClientConnectionsSubscription != null) {
      await _stopClientConnectionsSubscription!.cancel();
    }
    _startClientConnectionsSubscription = () async {
      final requestStatus = await _clientController?.startClientConnections();
      if (requestStatus != RequestStatus.acknowledged) {
        log.warning(
            'Failed to start wlan client connection. Request status: $requestStatus');
      }
    }()
        .asStream()
        .listen((_) {});
  }

  Future<void> _stopClientConnections() async {
    if (_startClientConnectionsSubscription != null) {
      await _startClientConnectionsSubscription!.cancel();
    }
    _stopClientConnectionsSubscription = () async {
      final requestStatus = await _clientController?.stopClientConnections();
      if (requestStatus != RequestStatus.acknowledged) {
        log.warning(
            'Failed to stop wlan client connection. Request status: $requestStatus');
      }
    }()
        .asStream()
        .listen((_) {});
  }

  Future<void> scanForNetworks() async {
    _scanForNetworksSubscription = () async {
      _scanResultIteratorProvider = policy.ScanResultIteratorProxy();
      await _clientController?.scanForNetworks(InterfaceRequest(
          _scanResultIteratorProvider?.ctrl.request().passChannel()));

      List<policy.ScanResult>? scanResults;
      try {
        scanResults = await _scanResultIteratorProvider?.getNext();
        _scannedNetworks.clear();
        while (scanResults != null && scanResults.isNotEmpty) {
          _scannedNetworks.addAll(scanResults);
          scanResults = await _scanResultIteratorProvider?.getNext();
        }
      } on Exception catch (e) {
        log.warning('Error encountered during scan: $e');
        // TODO(cwhitten): uncomment once fxb/87664 fixed
        // return;
      }
      onChanged();
    }()
        .asStream()
        .listen((_) {});
  }

  // TODO(cwhitten): simplify to _scannedNetworks.map(NetworkInformation.fromScanResult).toList();
  // once passing named contructors is supported by dart.
  List<NetworkInformation> get scannedNetworks =>
      networkInformationFromScannedNetworks(_scannedNetworks);

  List<NetworkInformation> networkInformationFromScannedNetworks(
      Set<policy.ScanResult> networks) {
    var networkInformationList = <NetworkInformation>[];
    for (var network in networks) {
      networkInformationList.add(NetworkInformation.fromScanResult(network));
    }
    return networkInformationList;
  }

  Uint8List ssidFromScannedNetwork(policy.ScanResult network) {
    return network.id!.ssid;
  }

  Future<void> connectToNetwork(String password) async {
    try {
      _connectToNetworkSubscription = () async {
        final credential = _targetNetwork.isOpen
            ? policy.Credential.withNone(policy.Empty())
            : policy.Credential.withPassword(
                Uint8List.fromList(password.codeUnits));

        if (_targetNetwork.networkIdentifier == null) {
          throw Exception(
              '$targetNetwork network does not have network identifier needed to connect.');
        }

        policy.NetworkIdentifier networkId = _targetNetwork.networkIdentifier!;

        final networkConfig =
            policy.NetworkConfig(id: networkId, credential: credential);

        await _clientController?.saveNetwork(networkConfig);

        final requestStatus = await _clientController?.connect(networkId);
        if (requestStatus != RequestStatus.acknowledged) {
          throw Exception(
              'connecting to $targetNetwork rejected: $requestStatus.');
        }

        // Refresh list of saved networks
        await getSavedNetworks();
      }()
          .asStream()
          .listen((_) {});
    } on Exception catch (e) {
      log.warning('Connecting to $targetNetwork failed: $e');
    }
  }

  String get currentNetwork => _monitor.currentNetwork();

  bool get connectionsEnabled => _monitor.connectionsEnabled();

  void _pollNetworksWithFailedCredentials(List<policy.NetworkIdentifier>? ids) {
    if (ids == null) {
      return;
    }
    for (var id in ids) {
      final foundNetwork = _savedNetworks.firstWhereOrNull((savedNetwork) =>
          listEquals(savedNetwork.id?.ssid, id.ssid) &&
          savedNetwork.id?.type == id.type);
      if (foundNetwork != null) {
        _networksWithFailedCredentials.add(foundNetwork);
      }
    }
  }

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

  Future<void> remove(NetworkInformation network) async {
    try {
      _removeNetworkSubscription = () async {
        final ssid = network.ssid;
        final securityType = network.securityType;
        final foundNetwork = _savedNetworks.firstWhereOrNull((savedNetwork) =>
            listEquals(savedNetwork.id?.ssid, ssid) &&
            savedNetwork.id?.type == securityType);

        if (foundNetwork == null) {
          throw Exception('$network not found in saved networks.');
        }

        final networkConfig = policy.NetworkConfig(
            id: foundNetwork.id, credential: foundNetwork.credential);

        await _clientController?.removeNetwork(networkConfig);

        // Refresh list of saved networks
        await getSavedNetworks();

        _networksWithFailedCredentials.removeWhere((networkConfig) =>
            listEquals(networkConfig.id?.ssid, foundNetwork.id?.ssid) &&
            networkConfig.id?.type == foundNetwork.id?.type);
      }()
          .asStream()
          .listen((_) {});
    } on Exception catch (e) {
      log.warning('Removing $network failed: $e');
    }
  }

  // TODO(cwhitten): simplify to _savedNetworks.map(NetworkInformation.fromNetworkConfig).toList();
  // once passing named contructors is supported by dart.
  List<NetworkInformation> get savedNetworks =>
      networkInformationFromSavedNetworks(_savedNetworks);

  List<NetworkInformation> networkInformationFromSavedNetworks(
      Set<policy.NetworkConfig> networks) {
    var networkInformationList = <NetworkInformation>[];
    for (var network in networks) {
      networkInformationList.add(NetworkInformation.fromNetworkConfig(
          network, _networksWithFailedCredentials));
    }
    return networkInformationList;
  }
}

class ClientStateUpdatesMonitor extends policy.ClientStateUpdates {
  final _binding = policy.ClientStateUpdatesBinding();
  policy.ClientStateSummary? _summary;
  late final VoidCallback _onChanged;
  late final void Function(List<policy.NetworkIdentifier>?)
      _pollNetworksWithFailedCredentials;

  ClientStateUpdatesMonitor(
      this._onChanged, this._pollNetworksWithFailedCredentials);

  InterfaceHandle<policy.ClientStateUpdates> getInterfaceHandle() =>
      _binding.wrap(this);

  policy.ClientStateSummary? getState() => _summary;

  bool connectionsEnabled() =>
      _summary?.state == policy.WlanClientState.connectionsEnabled;

  // Returns first found connected network.
  // TODO(fxb/79885): expand to return multiple connected networks.
  String currentNetwork() {
    final foundNetwork = _summary?.networks
        ?.firstWhereOrNull(
            (network) => network.state == policy.ConnectionState.connected)
        ?.id!
        .ssid
        .toList();
    return foundNetwork == null ? '' : utf8.decode(foundNetwork);
  }

  // Check for failed credentials and poll networks with failed credentials
  void _checkForFailedCredentials() {
    var foundNetworks = _summary?.networks?.where((network) =>
        network.status == policy.DisconnectStatus.credentialsFailed);
    var networkIDs = foundNetworks?.map((network) => network.id!).toList();
    if (networkIDs != null && networkIDs.isNotEmpty) {
      _pollNetworksWithFailedCredentials(networkIDs);
    }
  }

  @override
  Future<void> onClientStateUpdate(policy.ClientStateSummary summary) async {
    _summary = summary;
    _checkForFailedCredentials();
    _onChanged();
  }
}

/// Network information needed for UI
class NetworkInformation {
  // SSID used for lookup
  Uint8List? _ssid;
  // String representation of SSID in UI
  String? _name;
  // If network is able to be connected to
  bool _compatible = false;
  // Security type of network
  policy.SecurityType? _securityType;
  // If network has a failed connection attempt due to bad credentials
  // Only set true if failed credentials found
  bool credentialsFailed = false;
  // Network identifier (from scan results)
  policy.NetworkIdentifier? _networkIdentifier;
  // Network comes from saved networks (via network config)
  bool _isSaved = false;

  NetworkInformation();

  // Constructor for network config
  NetworkInformation.fromNetworkConfig(policy.NetworkConfig networkConfig,
      [Set<policy.NetworkConfig> networksWithFailedCredentials = const {}]) {
    _ssid = networkConfig.id?.ssid;
    _name = networkConfig.id?.ssid.toList() != null
        ? utf8.decode(networkConfig.id!.ssid.toList())
        : null;
    _compatible = true;
    _securityType = networkConfig.id?.type;
    if (networksWithFailedCredentials.isNotEmpty) {
      if (networksWithFailedCredentials
              .map((networkConfig) => networkConfig.id!)
              .firstWhereOrNull((networkIdentifier) =>
                  listEquals(networkIdentifier.ssid, _ssid) &&
                  networkIdentifier.type == _securityType) !=
          null) {
        credentialsFailed = true;
      }
    }
    _isSaved = true;
  }

  // Constructor for scan result
  NetworkInformation.fromScanResult(policy.ScanResult scanResult) {
    _networkIdentifier = scanResult.id;
    _ssid = scanResult.id?.ssid;
    _name = scanResult.id?.ssid.toList() != null
        ? utf8.decode(scanResult.id!.ssid.toList())
        : null;
    // Only allow valid characters in UI representation of SSID
    // TODO(fxb/92668): Allow special characters, such as emojis, in network names
    if (_name != null) {
      _name = _name!.replaceAll(RegExp(r'[^A-Za-z0-9()\[\]\s+.,;?&_-]'), '');
    }
    _compatible = scanResult.compatibility == policy.Compatibility.supported;
    _securityType = scanResult.id?.type;
  }

  Uint8List? get ssid => _ssid;

  String get name => _name ?? '';

  bool get compatible => _compatible;

  IconData get icon => _securityType == policy.SecurityType.none
      ? Icons.signal_wifi_4_bar
      : Icons.wifi_lock;

  bool get isOpen => _securityType == policy.SecurityType.none;

  bool get isWEP => _securityType == policy.SecurityType.wep;

  bool get isWPA => _securityType == policy.SecurityType.wpa;

  bool get isWPA2 => _securityType == policy.SecurityType.wpa2;

  bool get isWPA3 => _securityType == policy.SecurityType.wpa3;

  bool get isSaved => _isSaved;

  policy.SecurityType get securityType =>
      _securityType ?? policy.SecurityType.none;

  policy.NetworkIdentifier? get networkIdentifier => _networkIdentifier;
}
