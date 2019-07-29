// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl_fuchsia_netstack/fidl_async.dart' as net;
import 'package:fidl_fuchsia_wlan_service/fidl_async.dart' as wlan;
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:fuchsia_services/services.dart';
import 'package:lib.settings/debug.dart';
import 'package:lib.widgets/model.dart';

import 'access_point.dart';

const int _kConnectionScanInterval = 3;

/// The model for the wifi settings module.
///
/// All subclasses must connect the [wlan.WlanProxy] in their constructor
class WifiSettingsModel extends Model {
  /// How often to poll the wlan for wifi information.
  final Duration _updatePeriod = Duration(seconds: 3);

  /// How often to poll the wlan for available wifi networks.
  final Duration _scanPeriod = Duration(seconds: 40);

  final wlan.WlanProxy _wlanProxy = wlan.WlanProxy();

  final net.NetstackProxy _netstackProxy = net.NetstackProxy();

  /// Whether or not we've ever gotten the wifi status. Before this,
  /// we show the loading screen.
  bool _loading;
  bool _connecting;

  /// Whether or not there are any wireless adapters available on the system
  /// right now.
  bool _hasWlanInterface = true;

  /// Controlled by the toggle switch to determine if debug info is shown
  final ValueNotifier<bool> showDebugInfo = ValueNotifier<bool>(false);

  final _WifiDebugInfo _debugInfo = _WifiDebugInfo();

  wlan.WlanStatus _status;
  List<wlan.Ap> _scannedAps;

  AccessPoint _selectedAccessPoint;
  AccessPoint _failedAccessPoint;

  String _connectionResultMessage;

  Timer _updateTimer;
  Timer _scanTimer;

  StreamSubscription _interfacesChangedSubscription;

  /// Constructor.
  WifiSettingsModel()
      : _loading = true,
        _connecting = false {
    StartupContext.fromStartupInfo().incoming.connectToService(_wlanProxy);
    StartupContext.fromStartupInfo().incoming.connectToService(_netstackProxy);

    _interfacesChangedSubscription =
        _netstackProxy.onInterfacesChanged.listen(interfacesChanged);

    _scan();
    _updateTimer = Timer.periodic(_updatePeriod, (_) => _update());
    _scanTimer = Timer.periodic(_scanPeriod, (_) => _scan());

    showDebugInfo.addListener(notifyListeners);
  }

  /// The current list of available access points.
  ///
  /// Since scanning only works if there are no connected networks,
  /// this will only containb access points when unconnected.
  Iterable<AccessPoint> get accessPoints =>
      _scannedAps?.map((wlan.Ap ap) => AccessPoint(
            name: ap.ssid,
            signalStrength: ap.rssiDbm.toDouble(),
            isSecure: ap.isSecure,
          ));

  /// The access point that is either connected, or in the process of being
  /// connected.
  AccessPoint get connectedAccessPoint => _status?.currentAp != null
      ? AccessPoint(
          name: _status.currentAp.ssid,
          isSecure: _status.currentAp.isSecure,
          signalStrength: _status.currentAp.rssiDbm.toDouble())
      : null;

  /// Returns true if a connection is in progress
  bool get connecting => _connecting;

  /// Connection result message.  'null' if there is no connection result message.
  String get connectionResultMessage => _connectionResultMessage;

  /// A string describing the connection status of either the currently
  /// connected network, or the last attempted network depending
  /// on if connection was successful.
  String get connectionStatusMessage {
    String value;
    switch (state) {
      case wlan.State.associated:
        value = 'Connected';
        break;
      case wlan.State.associating:
      case wlan.State.joining:
      case wlan.State.scanning:
      case wlan.State.bss:
      case wlan.State.querying:
        value = 'Connecting';
        break;
      case wlan.State.authenticating:
        value = 'Authenticating...';
        break;
      default:
        value = 'Unknown';
    }
    return value;
  }

  /// The most recent error message.  'null' if there is no error.
  String get errorMessage => _status?.error?.description;

  /// The last network that was unsuccessfully connected to.
  AccessPoint get failedAccessPoint => _failedAccessPoint;

  bool get hasWifiAdapter => _hasWlanInterface;

  /// Whether or not the app has been loaded with the initial state
  bool get loading => _loading;

  /// Gets the currently selected access point.
  AccessPoint get selectedAccessPoint => _selectedAccessPoint;

  /// Sets the currently selected access point.
  set selectedAccessPoint(AccessPoint accessPoint) {
    if (_selectedAccessPoint != accessPoint) {
      if (!accessPoint.isSecure) {
        _connect(accessPoint);
      }
      _selectedAccessPoint = accessPoint;
      notifyListeners();
    }
  }

  /// The current state of the network
  wlan.State get state => _status?.state;

  DebugStatus get debugStatus => _debugInfo;

  /// Disconnects from the current network.
  Future<void> disconnect() async {
    await _wlanProxy.disconnect();

    _selectedAccessPoint = null;
    _loading = true;
    await _update();
  }

  /// Cleans up the model state.
  void dispose() {
    _updateTimer.cancel();
    _scanTimer.cancel();
    _interfacesChangedSubscription.cancel();
  }

  /// Listens for any changes to network interfaces.
  ///
  /// Sets whether or not there exists a wlan interface
  void interfacesChanged(List<net.NetInterface> interfaces) {
    _hasWlanInterface =
        interfaces.any((interface) => interface.name.contains('wlan'));
    notifyListeners();

    _debugInfo.interfaceUpdate(interfaces);
  }

  /// Called when the user dismisses the password dialog
  void onPasswordCanceled() {
    _selectedAccessPoint = null;
    notifyListeners();
  }

  /// Called when the password for a secure network has been set.
  void onPasswordEntered(String password) {
    _connect(_selectedAccessPoint, password);
  }

  Future<void> _connect(AccessPoint accessPoint, [String password]) async {
    _connecting = true;
    _scannedAps = null;

    final config = wlan.ConnectConfig(
        ssid: accessPoint.name,
        passPhrase: password ?? '',
        scanInterval: _kConnectionScanInterval,
        bssid: '');

    notifyListeners();
    _debugInfo.connectStart(config);

    final error = await _wlanProxy.connect(config);

    if (error.code == wlan.ErrCode.ok) {
      _connectionResultMessage = null;
      _failedAccessPoint = null;
    } else {
      _connectionResultMessage = error.description;
      _failedAccessPoint = selectedAccessPoint;
      _selectedAccessPoint = null;
      _connecting = false;
    }
    _debugInfo.connectComplete(error);
    await _update();
  }

  /// Remove duplicate and incompatible networks
  List<wlan.Ap> _dedupeAndRemoveIncompatible(wlan.ScanResult scanResult) {
    List<wlan.Ap> aps = <wlan.Ap>[];

    if (scanResult.error.code == wlan.ErrCode.ok) {
      // First sort APs by signal strength so when we de-dupe we drop the
      // weakest ones
      scanResult.aps.sort((wlan.Ap a, wlan.Ap b) => b.rssiDbm - a.rssiDbm);
      Set<String> seenNames = <String>{};

      for (wlan.Ap ap in scanResult.aps) {
        // Dedupe: if we've seen this ssid before, skip it.
        if (!seenNames.contains(ap.ssid) && ap.isCompatible) {
          aps.add(ap);
        }
        seenNames.add(ap.ssid);
      }
    }
    return aps;
  }

  Future<void> _scan() async {
    if (!_connecting) {
      _debugInfo.scanStart();

      final scanResult =
          await _wlanProxy.scan(const wlan.ScanRequest(timeout: 25));
      _scannedAps = _dedupeAndRemoveIncompatible(scanResult);
      notifyListeners();
      _debugInfo.scanComplete(scanResult);
    }
  }

  Future<void> _update() async {
    _debugInfo.updateStart();

    final status = await _wlanProxy.status();

    _status = status;
    _loading = false;

    if (status.state == wlan.State.associated ||
        status.error.code != wlan.ErrCode.ok) {
      _selectedAccessPoint = null;
      _connecting = false;
    }

    notifyListeners();
    _debugInfo.updateComplete(status);
  }
}

class _WifiDebugInfo extends DebugStatus {
  _WifiDebugInfo();

  void scanStart() {
    timestamp('[scan] begin');
  }

  void scanComplete(wlan.ScanResult result) {
    timestamp('[scan] complete');
    write('[scan] result', result.toString());
  }

  void updateStart() {
    timestamp('[status] begin');
  }

  void updateComplete(wlan.WlanStatus status) {
    timestamp('[status] complete');
    write('[status] result', status.toString());
  }

  void connectStart(wlan.ConnectConfig connectConfig) {
    timestamp('[connection] begin');
    write('[connection] begin config', connectConfig.toString());
  }

  void connectComplete(wlan.Error error) {
    timestamp('[connection] complete');
    write('[connection] result', error.toString());
  }

  void interfaceUpdate(List<net.NetInterface> interfaces) {
    timestamp('[interface] updated');
    write('[interace] list', interfaces.toString());
  }
}
