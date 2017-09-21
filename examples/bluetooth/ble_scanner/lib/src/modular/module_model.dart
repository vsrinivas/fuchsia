// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:lib.app.dart/app.dart';
import 'package:lib.app.fidl/service_provider.fidl.dart';
import 'package:lib.bluetooth.fidl/common.fidl.dart' as bt;
import 'package:lib.bluetooth.fidl/low_energy.fidl.dart' as ble;
import 'package:apps.modular.services.module/module_context.fidl.dart';
import 'package:apps.modular.services.story/link.fidl.dart';
import 'package:lib.logging/logging.dart';
import 'package:lib.widgets/modular.dart';

enum ConnectionState { notConnected, connecting, connected }

/// The [ModuleModel] for the BLE Scanner example.
class BLEScannerModuleModel extends ModuleModel implements ble.CentralDelegate {
  // Members that maintain the FIDL service connections.
  final ble.CentralProxy _central = new ble.CentralProxy();
  final ble.CentralDelegateBinding _delegateBinding =
      new ble.CentralDelegateBinding();

  // True if we have an active scan session.
  bool _isScanning = false;

  // True if a request to start a device scan is currently pending.
  bool _isScanRequestPending = false;

  // The current scan filter for current (if scanning) and future scan sessions.
  ble.ScanFilter _scanFilter;

  // Devices found during discovery.
  final Map<String, ble.RemoteDevice> _discoveredDevices =
      <String, ble.RemoteDevice>{};

  // Devices that are connected.
  final Map<String, ConnectionState> _connectedDevices =
      <String, ConnectionState>{};

  /// Constructor
  BLEScannerModuleModel(this.applicationContext) : super();

  /// We use the |applicationContext| to obtain a handle to the "bluetooth::low_energy::Central"
  /// environment service.
  final ApplicationContext applicationContext;

  /// True if we have an active scan session.
  bool get isScanning => _isScanning;

  /// True if a request to start a device scan is currently pending.
  bool get isScanRequestPending => _isScanRequestPending;

  /// Sets a scan filter. If scanning, this will immediately apply to the active scan session.
  /// Otherwise, it will be used next time a scan is requested.
  set scanFilter(ble.ScanFilter filter) {
    _scanFilter = filter;
    _discoveredDevices.clear();
    if (isScanning) _restartScan();
    notifyListeners();
  }

  /// Returns a list containing information about remote LE devices that have been discovered.
  Iterable<ble.RemoteDevice> get discoveredDevices => _discoveredDevices.values;

  /// Starts or stops a scan based on whether or not a scan is currently being performed.
  void toggleScan() {
    // The scan button will be disabled if a request is pending.
    assert(!_isScanRequestPending);

    if (isScanning) {
      _central.stopScan();
      return;
    }

    _restartScan();
    notifyListeners();
  }

  void _restartScan() {
    _isScanRequestPending = true;
    _central.startScan(_scanFilter, (bt.Status status) {
      _isScanRequestPending = false;
      notifyListeners();
    });
  }

  /// Returns the connection state for the peripheral with the given identifier.
  ConnectionState getPeripheralState(String id) =>
      _connectedDevices[id] ?? ConnectionState.notConnected;

  /// Initiates a connection to the given device.
  void connectPeripheral(String id) {
    if (getPeripheralState(id) != ConnectionState.notConnected) {
      log.info('Peripheral already connected or connecting (id: $id)');
      return;
    }

    _connectedDevices[id] = ConnectionState.connecting;
    notifyListeners();

    _central.connectPeripheral(id, (bt.Status status) {
      if (status.error != null) {
        log.info(
            'Failed to connect to device with (id: $id): ${status.error.description}');
        _connectedDevices.remove(id);
      } else {
        _connectedDevices[id] = ConnectionState.connected;
      }

      notifyListeners();
    });
  }

  /// Disconnects the requested peripheral.
  void disconnectPeripheral(String id) {
    if (getPeripheralState(id) != ConnectionState.connected) {
      log.info('Peripheral not connected (id: $id)');
      return;
    }

    _central.disconnectPeripheral(id, (bt.Status status) {
      log.info('Disconnect (id: $id, status: $status)');

      // The widgets will be notified by onPeripheralDisconnected.
    });
  }

  // ModuleModel overrides:

  @override
  void onReady(
    ModuleContext moduleContext,
    Link link,
    ServiceProvider incomingServices,
  ) {
    super.onReady(moduleContext, link, incomingServices);

    connectToService(applicationContext.environmentServices, _central.ctrl);
    _central.setDelegate(_delegateBinding.wrap(this));
  }

  @override
  void onStop() {
    super.onStop();
  }

  // ble.CentralDelegate overrides:

  @override
  void onScanStateChanged(bool scanning) {
    _isScanning = scanning;
    notifyListeners();
  }

  @override
  void onDeviceDiscovered(ble.RemoteDevice device) {
    _discoveredDevices[device.identifier] = device;
    notifyListeners();
  }

  @override
  void onPeripheralDisconnected(String id) {
    _connectedDevices.remove(id);
    notifyListeners();
  }
}
