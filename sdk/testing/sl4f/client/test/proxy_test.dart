// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:mockito/mockito.dart';
import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart';

class MockSl4f extends Mock implements Sl4f {}

void main(List<String> args) {
  MockSl4f sl4f;

  setUp(() {
    sl4f = MockSl4f();
  });

  test('open proxy with target port', () async {
    final proxy = TcpProxyController(sl4f);
    expect(proxy.proxyPorts, isEmpty);

    await proxy.openProxy(80);
    verify(sl4f.request('proxy_facade.OpenProxy', [80, 0]));

    await proxy.dropProxy(80);
    verify(sl4f.request('proxy_facade.DropProxy', 80));

    await proxy.stopAllProxies();
    verify(sl4f.request('proxy_facade.StopAllProxies'));
  });

  test('open proxy with proxy ports', () async {
    final proxy = TcpProxyController(sl4f, proxyPorts: [9000, 9001]);
    expect(proxy.proxyPorts, [9000, 9001]);

    await proxy.openProxy(80);
    verify(sl4f.request('proxy_facade.OpenProxy', [80, 9000]));

    await proxy.openProxy(8000);
    verify(sl4f.request('proxy_facade.OpenProxy', [8000, 9001]));

    // Opening any more proxies should throw SocketException.
    final isSocketException = TypeMatcher<SocketException>();
    expect(() => proxy.openProxy(8001), throwsA(isSocketException));

    await proxy.dropProxy(80);
    verify(sl4f.request('proxy_facade.DropProxy', 80));

    await proxy.stopAllProxies();
    verify(sl4f.request('proxy_facade.StopAllProxies'));
  });
}
