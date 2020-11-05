// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:http/http.dart' as http;
import 'package:test/test.dart';

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.TcpProxyController tcpProxyController;
  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
    tcpProxyController = sl4fDriver.proxy;
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  group('tcp proxy', () {
    test('sl4f reachable through proxy', () async {
      // sl4f itself is an http server, so use it to test our proxy.
      final proxyPort = await tcpProxyController.openProxy(80);
      await http.get(Uri.http('${sl4fDriver.target}:$proxyPort', '/'));
      // access through the proxy should fail once it is closed.
      await tcpProxyController.dropProxy(80);
      expect(http.get(Uri.http('${sl4fDriver.target}:$proxyPort', '/')),
          throwsException);
    });

    test('proxy supports multiple clients', () async {
      final proxyPort = await tcpProxyController.openProxy(80);
      await http.get(Uri.http('${sl4fDriver.target}:$proxyPort', '/'));
      // Attempting to create a proxy to the same port should return the existing port.
      final dupProxyPort = await tcpProxyController.openProxy(80);
      expect(dupProxyPort, equals(proxyPort));
      // proxy should remain open after first call to DropProxy because of the two calls to
      // OpenProxy. This allows two parts of a test to use the same proxy without one cancelling
      // it while the other still needs it.
      await tcpProxyController.dropProxy(80);
      await http.get(Uri.http('${sl4fDriver.target}:$proxyPort', '/'));
      // second call closes the proxy
      await tcpProxyController.dropProxy(80);
      expect(http.get(Uri.http('${sl4fDriver.target}:$proxyPort', '/')),
          throwsException);
    });

    test('stopAllProxies force terminates proxies', () async {
      final proxyPort = await tcpProxyController.openProxy(80);
      await http.get(Uri.http('${sl4fDriver.target}:$proxyPort', '/'));
      // Attempting to create a proxy to the same port should return the existing port.
      final dupProxyPort = await tcpProxyController.openProxy(80);
      expect(dupProxyPort, equals(proxyPort));
      // stopAllProxies should tear down the proxy even though 2 requests were made
      await tcpProxyController.stopAllProxies();
      expect(http.get(Uri.http('${sl4fDriver.target}:$proxyPort', '/')),
          throwsException);
    });

    test('throws exception if run out of tunneled ports', () async {
      // Get the number of tunneled ports provided by the user.
      final ports = Platform.environment['FUCHSIA_PROXY_PORTS']
          .split(',')
          .map((e) => e.trim())
          .map(int.parse);
      expect(ports, isNotEmpty);

      // Exhaust the number of tunneled ports.
      for (int i = 0; i < ports.length; i++) {
        await tcpProxyController.openProxy(80);
      }

      // Opening any more proxies should throw SocketException.
      final isSocketException = TypeMatcher<SocketException>();
      expect(tcpProxyController.openProxy(80), throwsA(isSocketException));
    }, skip: Platform.environment['FUCHSIA_PROXY_PORTS'] == null);
  });
}
