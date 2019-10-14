// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl_fuchsia_net/fidl_async.dart' as net;
import 'package:fidl_fuchsia_netstack/fidl_async.dart' as ns;
import 'package:fidl_fuchsia_hardware_ethernet/fidl_async.dart' as eth;
import 'package:flutter/widgets.dart';
import 'package:fuchsia_services/services.dart';
import 'package:lib.widgets/model.dart';

/// Provides netstack information.
class NetstackModel extends Model with TickerProviderModelMixin {
  /// The netstack containing networking information for the device.
  final ns.NetstackProxy netstack;

  StreamSubscription<bool> _reachabilitySubscription;
  StreamSubscription<List<ns.NetInterface>> _interfaceSubscription;
  final net.ConnectivityProxy connectivity = net.ConnectivityProxy();

  final ValueNotifier<bool> networkReachable = ValueNotifier<bool>(false);

  List<ns.NetInterface> _interfaces;

  /// Constructor.
  NetstackModel({this.netstack}) {
    StartupContext.fromStartupInfo().incoming.connectToService(connectivity);
    networkReachable.addListener(notifyListeners);
    _reachabilitySubscription =
        connectivity.onNetworkReachable.listen((reachable) {
      networkReachable.value = reachable;
    });
  }

  /// The current interfaces on the device.
  List<ns.NetInterface> get interfaces => _interfaces;

  void interfacesChanged(List<ns.NetInterface> interfaces) {
    _interfaces = interfaces
        .where((ns.NetInterface interface) =>
            interface.features & eth.infoFeatureLoopback == 0)
        .toList();
    notifyListeners();
  }

  /// Starts listening for netstack interfaces.
  void start() {
    _interfaceSubscription =
        netstack.onInterfacesChanged.listen(interfacesChanged);
  }

  /// Stops listening for netstack interfaces.
  void stop() {
    if (_interfaceSubscription != null) {
      _interfaceSubscription.cancel();
      _interfaceSubscription = null;
    }

    if (_reachabilitySubscription != null) {
      _reachabilitySubscription.cancel();
      _reachabilitySubscription = null;
    }
  }
}
