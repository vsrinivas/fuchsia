// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:zircon/zircon.dart';
import 'package:test/test.dart';
import 'package:fidl/fidl.dart';
import 'package:fidl_fidl_test_dartbindingstest/fidl_async.dart';

void main() async {
  group('proxy state', () {
    test('initial', () {
      final proxy = TestServerProxy();
      expect(proxy.ctrl.state, equals(InterfaceState.unbound));
    });

    test('request', () {
      final proxy = TestServerProxy();
      expect(proxy.ctrl.whenBound, completes);
      final request = proxy.ctrl.request();
      expect(proxy.ctrl.state, equals(InterfaceState.bound));
      expect(request.channel.handle.isValid, isTrue);
    });

    test('bind', () {
      final pair = InterfacePair<TestServer>();
      final proxy = TestServerProxy();
      expect(proxy.ctrl.whenBound, completes);
      proxy.ctrl.bind(pair.handle);
      expect(proxy.ctrl.state, equals(InterfaceState.bound));
      expect(pair.request.channel.handle.isValid, isTrue);
    });

    test('after bind', () {
      // Set up a bound proxy...
      var pair = InterfacePair<TestServer>();
      final proxy = TestServerProxy();
      proxy.ctrl.bind(pair.handle);

      // The bound proxy cannot be bound to a handle.
      pair = InterfacePair<TestServer>();
      expect(() => proxy.ctrl.bind(pair.handle), throwsA(anything));
      // The bound proxy cannot vend an interface request.
      expect(proxy.ctrl.request, throwsA(anything));
    });

    test('unbind', () {
      // Set up a bound proxy...

      final pair = InterfacePair<TestServer>();
      final proxy = TestServerProxy();
      proxy.ctrl.bind(pair.handle);

      expect(proxy.ctrl.whenClosed, completes);
      final handle = proxy.ctrl.unbind();
      expect(proxy.ctrl.state, equals(InterfaceState.closed));
      expect(handle.channel.handle.isValid, isTrue);
      expect(pair.request.channel.handle.isValid, isTrue);
    });

    test('close', () {
      // Set up a bound proxy...
      final pair = InterfacePair<TestServer>();
      final proxy = TestServerProxy();
      proxy.ctrl.bind(pair.handle);

      expect(proxy.ctrl.whenClosed, completes);
      proxy.ctrl.close();
      expect(proxy.ctrl.state, equals(InterfaceState.closed));
      final readResult = pair.request.channel.queryAndRead();
      expect(readResult.status, equals(ZX.ERR_PEER_CLOSED));
    });

    test('after close', () {
      // Set up a closed proxy...
      var pair = InterfacePair<TestServer>();
      final proxy = TestServerProxy();
      proxy.ctrl.bind(pair.handle);
      proxy.ctrl.close();

      // The closed proxy will never reach bound state.
      expect(proxy.ctrl.whenBound, throwsA(anything));
      // The closed proxy cannot be bound to a handle.
      pair = InterfacePair<TestServer>();
      expect(() => proxy.ctrl.bind(pair.handle), throwsA(anything));
      // The closed proxy cannot vend an interface request.
      expect(proxy.ctrl.request, throwsA(anything));
      // Calling close has no effect, nothing is thrown.
      proxy.ctrl.close();
    });
  });
}
