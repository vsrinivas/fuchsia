// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:zircon';
import 'dart:typed_data';
import 'dart:convert';

import 'package:test/test.dart';
import 'package:lib.fidl.dart/core.dart' as core;

/// Helper method to turn a [String] into a [ByteData] containing the
/// text of the string encoded as UTF-8.
ByteData utf8Bytes(final String text) {
  return new ByteData.view(new Uint8List.fromList(UTF8.encode(text)).buffer);
}

void main() {
  // NOTE: This only tests stream sockets.
  // We should add tests for datagram sockets.

  test('create socket', () {
    final pair = System.socketCreate();
    expect(pair.status, equals(core.NO_ERROR));
    expect(pair.first.isValid, isTrue);
    expect(pair.second.isValid, isTrue);
  });

  test('close socket', () {
    final pair = System.socketCreate();
    expect(pair.first.close(), equals(0));
    expect(pair.first.isValid, isFalse);
    expect(pair.second.isValid, isTrue);
    final firstResult = System.socketWrite(pair.first, new ByteData(1), 0);
    expect(firstResult.status, equals(core.ERR_BAD_HANDLE));
    final secondResult = System.socketWrite(pair.second, new ByteData(1), 0);
    expect(secondResult.status, equals(core.ERR_PEER_CLOSED));
  });

  test('read write socket', () {
    final pair = System.socketCreate();

    // When no data is available, ERR_SHOULD_WAIT is returned.
    expect(
        System.socketRead(pair.second, 1).status, equals(core.ERR_SHOULD_WAIT));

    final data = utf8Bytes("Hello, world");
    final writeResult = System.socketWrite(pair.first, data, 0);
    expect(writeResult.status, equals(core.NO_ERROR));

    final readResult = System.socketRead(pair.second, data.lengthInBytes);
    expect(readResult.status, equals(core.NO_ERROR));
    expect(readResult.numBytes, equals(data.lengthInBytes));
    expect(readResult.bytes.lengthInBytes, equals(data.lengthInBytes));
    expect(readResult.bytesAsUTF8String(), equals("Hello, world"));
  });

  test('partial read socket', () {
    final pair = System.socketCreate();
    final data = utf8Bytes("Hello, world");
    final writeResult = System.socketWrite(pair.first, data, 0);
    expect(writeResult.status, equals(core.NO_ERROR));

    final int shortLength = "Hello".length;
    final shortReadResult = System.socketRead(pair.second, shortLength);
    expect(shortReadResult.status, equals(core.NO_ERROR));
    expect(shortReadResult.numBytes, equals(shortLength));
    expect(shortReadResult.bytes.lengthInBytes, equals(shortLength));
    expect(shortReadResult.bytesAsUTF8String(), equals("Hello"));

    final int longLength = data.lengthInBytes * 2;
    final longReadResult = System.socketRead(pair.second, longLength);
    expect(longReadResult.status, equals(core.NO_ERROR));
    expect(longReadResult.numBytes, equals(data.lengthInBytes - shortLength));
    expect(longReadResult.bytes.lengthInBytes, equals(longLength));
    expect(longReadResult.bytesAsUTF8String(), equals(", world"));
  });

  test('partial write socket', () {
    final pair = System.socketCreate();
    final writeResult1 =
        System.socketWrite(pair.first, utf8Bytes("Hello, "), 0);
    expect(writeResult1.status, equals(core.NO_ERROR));
    final writeResult2 = System.socketWrite(pair.first, utf8Bytes("world"), 0);
    expect(writeResult2.status, equals(core.NO_ERROR));

    final readResult = System.socketRead(pair.second, 100);
    expect(readResult.status, equals(core.NO_ERROR));
    expect(readResult.numBytes, equals("Hello, world".length));
    expect(readResult.bytes.lengthInBytes, equals(100));
    expect(readResult.bytesAsUTF8String(), equals("Hello, world"));
  });

  test('async wait socket read', () async {
    final pair = System.socketCreate();
    final Completer<int> completer = new Completer();
    pair.first.asyncWait(core.MX_SOCKET_READABLE, core.MX_TIME_INFINITE,
        (int status, int pending) {
      completer.complete(status);
    });

    expect(completer.isCompleted, isFalse);

    System.socketWrite(pair.second, utf8Bytes("Hi"), 0);

    final int status = await completer.future;
    expect(status, equals(core.NO_ERROR));
  });

  test('async wait socket closed', () async {
    final pair = System.socketCreate();
    final Completer<int> completer = new Completer();
    pair.first.asyncWait(core.MX_SOCKET_PEER_CLOSED, core.MX_TIME_INFINITE,
        (int status, int pending) {
      completer.complete(status);
    });

    expect(completer.isCompleted, isFalse);

    pair.second.close();

    final int status = await completer.future;
    expect(status, equals(core.NO_ERROR));
  });
}
