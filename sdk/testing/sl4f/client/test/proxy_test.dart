// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:io';

import 'package:mockito/mockito.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

class MockSl4f extends Mock implements Sl4f {}

void main(List<String> args) {
  MockSl4f sl4f;

  setUp(() {
    sl4f = MockSl4f();
    when(sl4f.request('proxy_facade.OpenProxy', any))
        .thenAnswer((invocation) async {
      final ports = invocation.positionalArguments[1];
      return ports[1] == 0 ? 54321 : ports[1];
    });
  });

  test('open proxy without proxy ports', () async {
    final proxy = TcpProxyController(sl4f);
    expect(proxy.proxyPorts, isNull);

    await proxy.openProxy(80);
    verify(sl4f.request('proxy_facade.OpenProxy', [80, 0]));

    await proxy.dropProxy(80);
    verify(sl4f.request('proxy_facade.DropProxy', 80));

    await proxy.stopAllProxies();
    verify(sl4f.request('proxy_facade.StopAllProxies'));
  });

  test('open proxy with empty proxy ports', () async {
    final proxy = TcpProxyController(sl4f, proxyPorts: <int>[]);
    expect(proxy.proxyPorts, isNull);
  });

  test('open proxy with proxy ports', () async {
    final proxy = TcpProxyController(sl4f, proxyPorts: [9000, 9001]);
    expect(proxy.proxyPorts, [9000, 9001]);

    await proxy.openProxy(80);
    verify(sl4f.request('proxy_facade.OpenProxy', [80, 9000]));

    await proxy.openProxy(8000);
    verify(sl4f.request('proxy_facade.OpenProxy', [8000, 9001]));
    expect(proxy.proxyPorts, isEmpty);

    // Opening any more proxies should throw SocketException.
    await expectLater(proxy.openProxy(8001), throwsA(isA<SocketException>()));

    // Return a port and try again.
    await proxy.dropProxy(80);
    verify(sl4f.request('proxy_facade.DropProxy', 80));
    expect(proxy.proxyPorts, [9000]);

    await proxy.openProxy(8001);
    verify(sl4f.request('proxy_facade.OpenProxy', [8001, 9000]));

    await proxy.stopAllProxies();
    verify(sl4f.request('proxy_facade.StopAllProxies'));
  });
}
