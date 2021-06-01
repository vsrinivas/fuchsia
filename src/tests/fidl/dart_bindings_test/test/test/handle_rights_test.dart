// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl/fidl.dart';
import 'package:server_test/server.dart';
import 'package:test/test.dart';
import 'package:zircon/zircon.dart';

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
}
