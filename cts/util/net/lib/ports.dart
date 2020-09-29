// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

/// Repeatedly finds a probably-unused port on localhost and passes it to
/// [tryPort] until it binds successfully.
///
/// [tryPort] should return a non-`null` value or a Future completing to a
/// non-`null` value once it binds successfully. This value will be returned
/// by [getUnusedPort] in turn.
///
/// This is necessary for ensuring that our port binding isn't flaky for
/// applications that don't print out the bound port.
Future<T> getUnusedPort<T>(FutureOr<T> Function(int port) tryPort) async {
  T value;
  await Future.doWhile(() async {
    value = await tryPort(await getUnsafeUnusedPort());
    return value == null;
  });
  return value;
}

/// This bool is intended to guard from needing 2 binds for every port that we need.
/// Once an IPv6 bind fails, we assume it won't ever work and use IPv4 only
/// from there on out.
var _maySupportIPv6 = true;

/// Returns a port that is probably, but not definitely, not in use.
///
/// This has a built-in race condition: another process may bind this port at
/// any time after this call has returned. If at all possible, callers should
/// use [getUnusedPort] instead.
Future<int> getUnsafeUnusedPort() async {
  int port;
  if (_maySupportIPv6) {
    try {
      final socket = await RawServerSocket.bind(InternetAddress.loopbackIPv6, 0,
          v6Only: true);
      port = socket.port;
      await socket.close();
    } on SocketException {
      _maySupportIPv6 = false;
    }
  }
  if (!_maySupportIPv6) {
    final socket = await RawServerSocket.bind(InternetAddress.loopbackIPv4, 0);
    port = socket.port;
    await socket.close();
  }
  return port;
}
