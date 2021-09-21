// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:async';
import 'dart:typed_data';

import 'package:fidl/fidl.dart';
import 'package:fidl_fidl_test_dartbindingstest/fidl_async.dart';
import 'package:server_test/server.dart';
import 'package:test/test.dart';
import 'package:zircon/zircon.dart';

class Ordinals {
  static const int sendEvent = 0x4313c1541d0f2e86;
  static const int sendChannel = 0x1c036ce2684ad5b2;
}

class HandleRightsTestServerImpl extends HandleRightsTestServer {
  @override
  Future<void> sendEvent(Handle h) {
    throw FidlError('send event received');
  }

  @override
  Future<void> sendChannel(Channel h) {
    throw FidlError('send channel received');
  }
}

void main() async {
  TestServerInstance server;

  group('end-to-end handle rights', () {
    setUpAll(() async {
      server = TestServerInstance();
      await server.start();
    });

    tearDownAll(() async {
      await server.stop();
      server = null;
    });

    test('sending channel as event from client', () async {
      ChannelPair channelPair = ChannelPair();
      try {
        await server.proxy.sendEvent(channelPair.first.handle);
        fail('expected exception');
        // The fail() line is hit when "on FidlError" is added to the catch clause.
        // This is odd because the existance of an "on" shouldn't effect whether sendEvent throws
        // an error. To work around this, "on FidlError" was removed from this catch.
        // ignore: avoid_catches_without_on_clauses
      } catch (err) {
        /* expected */
      } // ignore: unused_catch_clause
      channelPair.first.close();
      channelPair.second.close();
    });

    test('returning channel as event from server', () async {
      ChannelPair channelPair = ChannelPair();
      try {
        await server.proxy.echoChannelAsEvent(channelPair.first);
        fail('expected exception');
      } on FidlError catch (err) {/* expected */} // ignore: unused_catch_clause
      channelPair.first.close();
      channelPair.second.close();
    });

    test('sending channel as fidl event', () async {
      ChannelPair channelPair = ChannelPair();
      try {
        await server.proxy.eventEvent(channelPair.first.handle);
        fail('expected exception');
      } on FidlError catch (err) {/* expected */} // ignore: unused_catch_clause
      channelPair.first.close();
      channelPair.second.close();
    });
  });

  // Tests that handle rights checks are happening on both send and receive side.
  group('handle rights directionality', () {
    Future<int> changeOrdinal(
        Channel channelIn,
        int ordinalIn,
        Channel channelOut,
        int ordinalOut,
        int targetType,
        int targetRights) async {
      Completer completer = Completer();
      channelIn.handle.asyncWait(Channel.READABLE | Channel.PEER_CLOSED,
          (int a, int b) {
        completer.complete();
      });
      await completer.future;
      ReadEtcResult readEtcResult = channelIn.queryAndReadEtc();
      ByteData bytes = readEtcResult.bytes;
      int readOrdinal = Uint64List.sublistView(bytes, 8, 16).first;
      expect(readOrdinal, equals(ordinalIn));

      Uint64List.sublistView(bytes, 8, 16).first = ordinalOut;
      List<HandleDisposition> handleDispositions = readEtcResult.handleInfos
          .map((handleInfo) => HandleDisposition(
              ZX.HANDLE_OP_MOVE, handleInfo.handle, targetType, targetRights))
          .toList();
      return channelOut.writeEtc(bytes, handleDispositions);
    }

    test('sending channel as event from client - send side error', () async {
      ChannelPair payloadPair = ChannelPair();

      final HandleRightsTestServerProxy proxy = HandleRightsTestServerProxy();
      Channel clientChannel = proxy.ctrl.request().passChannel();

      await proxy.sendEvent(payloadPair.first.handle);
      clientChannel.close();
      payloadPair.first.close();
      payloadPair.second.close();
    });

    test('sending channel as event from client - receive side error', () async {
      ChannelPair payloadPair = ChannelPair();
      ChannelPair proxyPair = ChannelPair();

      final HandleRightsTestServerProxy proxy = HandleRightsTestServerProxy();
      Channel clientChannel = proxy.ctrl.request().passChannel();

      HandleRightsTestServerBinding binding = HandleRightsTestServerBinding();
      HandleRightsTestServerImpl server = HandleRightsTestServerImpl();
      binding.bind(server, InterfaceRequest<Channel>(proxyPair.second));

      await proxy.sendChannel(payloadPair.first);
      int writeStatus = await changeOrdinal(
          clientChannel,
          Ordinals.sendChannel,
          proxyPair.first,
          Ordinals.sendEvent,
          ZX.OBJ_TYPE_EVENT,
          ZX.RIGHT_SAME_RIGHTS);
      expect(writeStatus, equals(ZX.ERR_WRONG_TYPE));
      clientChannel.close();
      payloadPair.first.close();
      payloadPair.second.close();
      proxyPair.first.close();
      proxyPair.second.close();
    });
  });
}
