// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:pedantic/pedantic.dart';
import 'package:zircon/zircon.dart';
import 'package:fidl_fidl_test_dartbindingstest/fidl_async.dart';
import 'package:test/test.dart';
import 'package:fidl/fidl.dart';
import './server.dart';

class TestAsyncBinding extends AsyncBinding<EmptyImpl> {
  // ignore: empty_constructor_bodies
  TestAsyncBinding() : super(r'TestAsyncBinding') {}
  @override
  void handleMessage(Message message, MessageSink respond) {
    assert(false, 'should fail in _handleReadable');
  }
}

class EmptyImpl {}

void main() {
  group('magic number write', () {
    test('requests', () async {
      final TestServerProxy proxy = TestServerProxy();
      Channel server = proxy.ctrl.request().passChannel();
      await proxy.oneWayStringArg('foo');
      final ReadResult result = server.queryAndRead();
      final Message message = Message.fromReadResult(result);
      expect(message.magic, equals(kMagicNumberInitial));
    });

    test('events', () async {
      final TestServerProxy proxy = TestServerProxy();
      Channel client = proxy.ctrl.request().passChannel();
      await proxy.sendStringEvent('bar');
      final ReadResult result = client.queryAndRead();
      final Message message = Message.fromReadResult(result);
      expect(message.magic, equals(kMagicNumberInitial));
    });

    test('responses', () async {
      final TestServerInstance server = TestServerInstance();
      await server.start();
      Completer magicNumberCompleter = Completer();
      // monkey patch the proxy controller to resolve responses to the magic
      // number of the response message
      server.proxy.ctrl.onResponse = (Message message) {
        magicNumberCompleter.complete(message.magic);
      };

      // this request will complete with an error after server.stop() since
      // onResponse will never respond to it
      unawaited(server.proxy.twoWayStringArg('baz').catchError((e) {}));
      int magic = await magicNumberCompleter.future;
      expect(magic, equals(kMagicNumberInitial));

      await server.stop();
    });
  });

  group('magic number read', () {
    test('requests', () {
      ChannelPair pair = ChannelPair();
      expect(pair.status, equals(ZX.OK));

      final client = pair.first;
      TestAsyncBinding().bind(EmptyImpl(), InterfaceRequest(pair.second));

      final encoder = Encoder()
        ..encodeMessageHeader(0, 0)
        ..encodeUint8(0, kMessageMagicOffset);
      client.write(encoder.message.data, encoder.message.handles);
    });
  });
}
