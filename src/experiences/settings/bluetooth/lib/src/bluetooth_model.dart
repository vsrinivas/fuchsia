// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'package:fidl_fuchsia_bluetooth/fidl_async.dart';
import 'package:fidl_fuchsia_bluetooth_control/fidl_async.dart';
import 'package:flutter/foundation.dart';
import 'package:fuchsia_services/services.dart';
import 'package:lib.widgets/model.dart';

const Duration _deviceListRefreshInterval = Duration(seconds: 5);

/// Model containing state needed for the bluetooth settings app.
class BluetoothSettingsModel extends Model {
  /// Bluetooth controller proxy.
  final ControlProxy _control = ControlProxy();
  final BluetoothSettingsPairingDelegate _pairingDelegate =
      BluetoothSettingsPairingDelegate();

  List<AdapterInfo> _adapters;
  AdapterInfo _activeAdapter;
  List<RemoteDevice> _remoteDevices = [];
  Timer _sortListTimer;
  bool _discoverable = true;
  final List<StreamSubscription> _listeners = [];

  bool _debugMode = false;

  BluetoothSettingsModel() {
    _onStart();
  }

  Future<void> setDiscoverable({bool discoverable}) async {
    await _control.setDiscoverable(discoverable);
    _discoverable = discoverable;
    notifyListeners();
  }

  set debugMode(bool enabled) {
    _debugMode = enabled;
    notifyListeners();
  }

  bool get discoverable => _discoverable;
  bool get debugMode => _debugMode;

  /// TODO(ejia): handle failures and error messages
  Future<void> connect(RemoteDevice device) async {
    await _control.connect(device.identifier);
  }

  Future<void> disconnect(RemoteDevice device) async {
    await _control.disconnect(device.identifier);
  }

  /// Bluetooth devices that are seen, but are not connected.
  Iterable<RemoteDevice> get availableDevices =>
      _remoteDevices.where((device) => !device.bonded);

  /// Bluetooth devices that are connected to the current adapter.
  Iterable<RemoteDevice> get knownDevices =>
      _remoteDevices.where((remoteDevice) => remoteDevice.bonded);

  /// The current adapter that is being used
  AdapterInfo get activeAdapter => _activeAdapter;

  /// All adapters that are not currently active.
  Iterable<AdapterInfo> get inactiveAdapters =>
      _adapters?.where((adapter) => activeAdapter.address != adapter.address) ??
      [];

  PairingStatus get pairingStatus => _pairingDelegate.pairingStatus;

  void acceptPairing(String passkeyInput) {
    _pairingDelegate?.completePairing(accept: true, passkeyInput: passkeyInput);
  }

  void rejectPairing() {
    _pairingDelegate?.completePairing(accept: false);
  }

  Future<void> _onStart() async {
    StartupContext.fromStartupInfo().incoming.connectToService(_control);
    await _refresh();

    // Sort the list by signal strength every few seconds.
    _sortListTimer = Timer.periodic(_deviceListRefreshInterval, (_) {
      _remoteDevices
          .sort((a, b) => (b.rssi?.value ?? 0).compareTo(a.rssi?.value ?? 0));
      _refresh();
    });

    // Just for first draft purposes, refresh whenever there are any changes.
    // TODO: handle errors, refresh more gracefully
    _listeners
      ..add(_control.onActiveAdapterChanged.listen((_) => _refresh()))
      ..add(_control.onAdapterRemoved.listen((_) => _refresh()))
      ..add(_control.onAdapterUpdated.listen((_) => _refresh()))
      ..add(_control.onDeviceUpdated.listen((device) {
        int index =
            _remoteDevices.indexWhere((d) => d.identifier == device.identifier);
        if (index != -1) {
          // Existing device, just update in-place.
          _remoteDevices[index] = device;
        } else {
          // New device, add to bottom of list.
          _remoteDevices.add(device);
        }
        notifyListeners();
      }))
      ..add(_control.onDeviceRemoved.listen((deviceId) {
        _removeDeviceFromList(deviceId);
        notifyListeners();
      }));

    await _control.requestDiscovery(true);
    await _control.setDiscoverable(true);
    await _control
        .setPairingDelegate(PairingDelegateBinding().wrap(_pairingDelegate));
  }

  void _removeDeviceFromList(String deviceId) {
    _remoteDevices.removeWhere((device) => device.identifier == deviceId);
  }

  /// Updates all the state that the model gets from bluetooth.
  Future<void> _refresh() async {
    _adapters = await _control.getAdapters();
    _activeAdapter = await _control.getActiveAdapterInfo();
    _remoteDevices = await _control.getKnownRemoteDevices();
    notifyListeners();
  }

  /// Closes the connection to the bluetooth control, thus ending active
  /// scanning.
  void dispose() {
    _control.ctrl.close();
    _sortListTimer.cancel();
    _pairingDelegate.dispose();
    for (StreamSubscription subscription in _listeners) {
      subscription.cancel();
    }
  }
}

class PairingStatus {
  final String displayedPasskey;
  final PairingMethod pairingMethod;
  final RemoteDevice device;
  int digitsEntered = 0;
  bool completing = false;

  PairingStatus(this.displayedPasskey, this.pairingMethod, this.device);
}

/// Used to capture events from bluetooth pairing.
class BluetoothSettingsPairingDelegate extends PairingDelegate
    with ChangeNotifier {
  Completer<PairingDelegate$OnPairingRequest$Response> _completer;
  PairingStatus pairingStatus;

  final StreamController<PairingDelegate$OnLocalKeypress$Response>
      _localKeypressController = StreamController.broadcast();

  void localKeypress(PairingKeypressType pressType) {
    _localKeypressController.add(PairingDelegate$OnLocalKeypress$Response(
        pairingStatus.device.identifier, pressType));
  }

  void completePairing({bool accept, String passkeyInput}) {
    _completer?.complete(
        PairingDelegate$OnPairingRequest$Response(accept, passkeyInput));
    _completer = null;
    notifyListeners();
  }

  @override
  Stream<PairingDelegate$OnLocalKeypress$Response> get onLocalKeypress =>
      _localKeypressController.stream;

  @override
  Future<void> onPairingComplete(String deviceId, Status status) async {
    // TODO(armansito): Display an error message when pairing fails.
    pairingStatus = null;
    _completer = null;
    notifyListeners();
  }

  @override
  Future<PairingDelegate$OnPairingRequest$Response> onPairingRequest(
      RemoteDevice device,
      PairingMethod method,
      String displayedPasskey) async {
    // End existing pairing.
    _completer
        ?.complete(PairingDelegate$OnPairingRequest$Response(false, null));

    // TODO(armansito): It would be better to handle multiple pairing requests
    // simultaneously via separate dialogs.
    pairingStatus = PairingStatus(displayedPasskey, method, device);
    notifyListeners();

    _completer = Completer<PairingDelegate$OnPairingRequest$Response>();
    return _completer.future;
  }

  @override
  Future<void> onRemoteKeypress(
      String deviceId, PairingKeypressType keypress) async {
    assert(pairingStatus.device.identifier == deviceId);
    switch (keypress) {
      case PairingKeypressType.digitEntered:
        pairingStatus.digitsEntered++;
        break;
      case PairingKeypressType.digitErased:
        pairingStatus.digitsEntered++;
        break;
      case PairingKeypressType.passkeyCleared:
        pairingStatus.digitsEntered = 0;
        break;
      case PairingKeypressType.passkeyEntered:
        pairingStatus.completing = true;
    }
    notifyListeners();
  }

  @override
  void dispose() {
    super.dispose();
    _localKeypressController.close();
  }
}
