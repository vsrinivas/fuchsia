// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';
import 'sl4f_client.dart';

/// `TcpProxyController` is a utility for host-driven tests that need to access ports on a DUT that
/// only permit localhost connections.
///
/// `TcpProxyController` opens ports that are accessible from the host and tunnels connections from
/// the open ports to the restricted ports. Example use cases include access to Webdriver and
/// Flutter debug ports.
///
/// Note that unlike ssh port forwarding, the accessible ports are located on the DUT, rather than
/// on the host.
class TcpProxyController {
  final Sl4f _sl4f;

  int _nextPortIndex = 0;

  /// The pool of ports to use that are explicitly forwarded by the user. These
  /// are drained in FIFO order for every [openProxy] call.
  final List<int> proxyPorts;

  TcpProxyController(this._sl4f, {this.proxyPorts = const <int>[]});

  /// Open a tunnel to [targetPort] on the DUT.
  ///
  /// Returns the port on the DUT through which the tunnel is accessible.
  Future<int> openProxy(int targetPort) async => await _sl4f
      .request('proxy_facade.OpenProxy', [targetPort, _nextProxyPort()]);

  /// Stop a tunnel to [targetPort] on the DUT.
  ///
  /// In case that n requests were made to open the proxy, the proxy is closed only after n calls
  /// to dropProxy with [targetPort] are made.
  Future<void> dropProxy(int targetPort) async =>
      await _sl4f.request('proxy_facade.DropProxy', targetPort);

  /// Forcibly terminate all proxies.
  ///
  /// Terminates all proxies, ignoring the number of clients that requested proxies.
  /// This method is intended for cleanup after a test only.
  Future<void> stopAllProxies() async =>
      await _sl4f.request('proxy_facade.StopAllProxies');

  // Returns the next proxy port from the list of ports provided during
  // construction. If no ports where provided, or `_nextPortIndex` is out of
  // range, it returns 0.
  int _nextProxyPort() {
    if (_nextPortIndex < proxyPorts.length) {
      return proxyPorts[_nextPortIndex++];
    } else {
      if (proxyPorts.isNotEmpty) {
        // Halt the test and notify the user that they ran out of proxy ports.
        throw SocketException('Tcp proxy ran out of ports.');
      }
      return 0;
    }
  }
}
