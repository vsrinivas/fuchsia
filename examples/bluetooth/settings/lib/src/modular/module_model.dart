// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:lib.app.dart/app.dart';
import 'package:lib.app.fidl/service_provider.fidl.dart';
import 'package:lib.bluetooth.fidl/common.fidl.dart' as common;
import 'package:lib.bluetooth.fidl/control.fidl.dart'
    as bluetooth;
import 'package:lib.module.fidl/module_context.fidl.dart';
import 'package:lib.story.fidl/link.fidl.dart';
import 'package:lib.logging/logging.dart';
import 'package:lib.widgets/modular.dart';

/// The [ModuleModel] for the Settings example.
class SettingsModuleModel extends ModuleModel
    implements bluetooth.AdapterManagerDelegate, bluetooth.AdapterDelegate {
  // Members that maintain the FIDL service connections.
  final bluetooth.AdapterManagerProxy _adapterManager =
      new bluetooth.AdapterManagerProxy();
  final bluetooth.AdapterDelegateBinding _adBinding =
      new bluetooth.AdapterDelegateBinding();
  final bluetooth.AdapterManagerDelegateBinding _amdBinding =
      new bluetooth.AdapterManagerDelegateBinding();

  // Contains information about the Bluetooth adapters that are on the system.
  final Map<String, bluetooth.AdapterInfo> _adapters =
      <String, bluetooth.AdapterInfo>{};

  // The current system's active Bluetooth adapter. We assign these fields when the AdapterManager
  // service notifies us.
  String _activeAdapterId;
  bluetooth.AdapterProxy _activeAdapter;

  // True if we have an active discovery session.
  bool _isDiscovering = false;

  // True if a request to start/stop discovery is currently pending.
  bool _isDiscoveryRequestPending = false;

  // Devices found during discovery.
  final Map<String, bluetooth.RemoteDevice> _discoveredDevices =
      <String, bluetooth.RemoteDevice>{};

  /// Constructor
  SettingsModuleModel(this.applicationContext) : super();

  /// We use the |applicationContext| to obtain a handle to the "bluetooth::control::AdapterManager"
  /// environment service.
  final ApplicationContext applicationContext;

  /// Public accessors for the private fields above.
  Iterable<bluetooth.AdapterInfo> get adapters => _adapters.values;

  /// Returns true if at least one adapter exists on the system.
  bool get isBluetoothAvailable => _adapters.isNotEmpty;

  /// Returns true if an active adapter exists on the current system.
  bool get hasActiveAdapter => _activeAdapterId != null;

  /// Returns true, if the adapter with the given ID is the current active adapter.
  bool isActiveAdapter(String adapterId) =>
      hasActiveAdapter && (_activeAdapterId == adapterId);

  /// Returns information about the current active adapter.
  bluetooth.AdapterInfo get activeAdapterInfo =>
      hasActiveAdapter ? _adapters[_activeAdapterId] : null;

  /// Returns true if a request to start/stop discovery is currently pending.
  bool get isDiscoveryRequestPending => _isDiscoveryRequestPending;

  /// Returns true if device discovery is in progress.
  bool get isDiscovering => _isDiscovering;

  /// Returns a list of Bluetooth devices that have been discovered.
  Iterable<bluetooth.RemoteDevice> get discoveredDevices =>
      _discoveredDevices.values;

  /// Returns a string that describes the current active adapter which can be displayed to the user.
  String get activeAdapterDescription =>
      activeAdapterInfo?.address ?? 'no adapters';

  /// Tells the AdapterManager service to set the adapter identified by |id| as the new active
  /// adapter. This affects the entire system as the active adapter currently in use by all current
  /// Bluetooth service clients will change.
  void setActiveAdapter(String id) {
    _adapterManager.setActiveAdapter(id, (common.Status status) {
      notifyListeners();
    });
  }

  /// Like setActiveAdapter but uses an index into |adapters| to identify the adapter. |index| must
  /// be a valid index.
  void setActiveAdapterByIndex(int index) {
    setActiveAdapter(adapters.elementAt(index).identifier);
  }

  /// Starts or stops a general discovery session.
  void toggleDiscovery() {
    log.info('toggleDiscovery');
    assert(_activeAdapter != null);
    assert(!_isDiscoveryRequestPending);

    _isDiscoveryRequestPending = true;
    var cb = (common.Status status) {
      _isDiscoveryRequestPending = false;
      notifyListeners();
    };

    if (isDiscovering) {
      log.info('Stop discovery');
      _activeAdapter.stopDiscovery(cb);
    } else {
      log.info('Start discovery');
      _activeAdapter.startDiscovery(cb);
    }

    notifyListeners();
  }

  @override
  void onReady(
    ModuleContext moduleContext,
    Link link,
    ServiceProvider incomingServices,
  ) {
    super.onReady(moduleContext, link, incomingServices);

    connectToService(
        applicationContext.environmentServices, _adapterManager.ctrl);
    _adapterManager.setDelegate(_amdBinding.wrap(this));
  }

  @override
  void onStop() {
    _activeAdapter.ctrl.close();
    _adapterManager.ctrl.close();
    super.onStop();
  }

  // bluetooth.AdapterManagerDelegate overrides:

  @override
  void onActiveAdapterChanged(bluetooth.AdapterInfo activeAdapter) {
    log.info('onActiveAdapterChanged: ${activeAdapter?.identifier ?? 'null'}');

    // Reset the state of all running procedures as the active adapter has changed.
    _isDiscovering = false;
    _isDiscoveryRequestPending = false;
    _discoveredDevices.clear();

    // Clean up our current Adapter interface connection if there is one.
    if (_activeAdapter != null) {
      _adBinding.close();
      _activeAdapter.ctrl.close();
    }

    _activeAdapterId = activeAdapter?.identifier;
    if (_activeAdapterId == null) {
      _activeAdapter = null;
    } else {
      _activeAdapter = new bluetooth.AdapterProxy();
      _adapterManager.getActiveAdapter(_activeAdapter.ctrl.request());
      _activeAdapter.setDelegate(_adBinding.wrap(this));
    }

    notifyListeners();
  }

  @override
  void onAdapterAdded(bluetooth.AdapterInfo adapter) {
    log.info('onAdapterAdded: ${adapter.identifier}');
    _adapters[adapter.identifier] = adapter;
    notifyListeners();
  }

  @override
  void onAdapterRemoved(String identifier) {
    log.info('onAdapterRemoved: $identifier');
    _adapters.remove(identifier);
    if (_adapters.isEmpty) _activeAdapterId = null;
    notifyListeners();
  }

  // bluetooth.AdapterDelegate overrides:

  @override
  void onAdapterStateChanged(bluetooth.AdapterState state) {
    log.info('onAdapterStateChanged');
    if (state.discovering == null) return;

    _isDiscovering = state.discovering.value;
    log.info(
        'Adapter state change: ${_isDiscovering ? '' : 'not'} discovering');
    notifyListeners();
  }

  @override
  void onDeviceDiscovered(bluetooth.RemoteDevice device) {
    _discoveredDevices[device.identifier] = device;
    notifyListeners();
  }
}
