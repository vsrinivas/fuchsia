// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:io';

import 'package:logging/logging.dart';

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
  final _log = Logger('tcp_proxy');

  final Sl4f _sl4f;

  /// The pool of ports to use that are explicitly forwarded by the user. It is
  /// a FIFO circular queue that is used by [openProxy] and [dropProxy].
  final List<int> proxyPorts;

  /// A map from currently proxied target ports to their proxy ports.
  final Map<int, int> _targetToProxyPorts = <int, int>{};

  /// Creates a TcpProxyController. If [proxyPorts] isn't provided or is empty,
  /// the proxy will pick available ports itself.
  TcpProxyController(this._sl4f, {List<int> proxyPorts})
      : proxyPorts =
            (proxyPorts != null && proxyPorts.isNotEmpty) ? proxyPorts : null;

  /// Open a tunnel to [targetPort] on the DUT.
  ///
  /// Returns the port on the DUT through which the tunnel is accessible. Pick
  /// the head of [proxyPorts] if it was provided to the constructor; otherwise,
  /// the proxy will pick an available port itself.
  ///
  /// Do not call with the same [targetPort] multiple times.
  Future<int> openProxy(int targetPort) async {
    int wantProxyPort = 0;
    if (proxyPorts != null) {
      if (proxyPorts.isEmpty) {
        // Halt the test and notify the user that they ran out of proxy ports.
        throw SocketException('Tcp proxy ran out of ports.');
      }
      wantProxyPort = proxyPorts.removeAt(0);
    }
    final gotProxyPort = await _sl4f
        .request('proxy_facade.OpenProxy', [targetPort, wantProxyPort]);
    _targetToProxyPorts[targetPort] = gotProxyPort;
    _log.fine('Forwarding TCP ports on DUT: $targetPort -> $gotProxyPort');
    return gotProxyPort;
  }

  /// Stop a tunnel to [targetPort] on the DUT.
  ///
  /// If the proxy port was retrieved from [proxyPorts], it will be returned to
  /// the back of it for reuse.
  ///
  /// Note: In case that n requests were made to proxy [targetPort], the proxy
  /// is closed only after n calls to dropProxy with [targetPort] are made.
  Future<void> dropProxy(int targetPort) async {
    _log.fine('Cancelling TCP port forwarding on DUT for $targetPort');
    await _sl4f.request('proxy_facade.DropProxy', targetPort);
    final releasedProxyPort = _targetToProxyPorts.remove(targetPort);
    if (releasedProxyPort != null) {
      proxyPorts?.add(releasedProxyPort);
    }
  }

  /// Forcibly terminate all proxies.
  ///
  /// Terminates all proxies, ignoring the number of clients that requested
  /// proxies. This method is intended for cleanup after a test only.
  Future<void> stopAllProxies() async {
    await _sl4f.request('proxy_facade.StopAllProxies');
    proxyPorts?.addAll(_targetToProxyPorts.values);
    _targetToProxyPorts.clear();
  }
}
