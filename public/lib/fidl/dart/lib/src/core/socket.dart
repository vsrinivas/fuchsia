// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of core;

class Socket {
  Handle handle;

  Socket(this.handle);

  WriteResult write(ByteData data) {
    if (handle == null)
      return const WriteResult(ZX.ERR_INVALID_ARGS);

    return System.socketWrite(handle, data, 0);
  }

  ReadResult read(int numBytes) {
    if (handle == null)
      return const ReadResult(ZX.ERR_INVALID_ARGS);

    return System.socketRead(handle, numBytes);
  }

  void close() {
    handle.close();
    handle = null;
  }

  @override
  String toString() => 'Socket($handle)';
}

class SocketPair {
  factory SocketPair() {
    final HandlePairResult result = System.socketCreate(ZX.SOCKET_STREAM);

    return new SocketPair._(
      status: result.status,
      socket0: new Socket(result.first),
      socket1: new Socket(result.second),
    );
  }

  SocketPair._({
    this.status: ZX.OK,
    this.socket0,
    this.socket1,
  });

  final int status;
  Socket socket0;
  Socket socket1;

  Socket passSocket0() {
    final Socket result = socket0;
    socket0 = null;
    return result;
  }

  Socket passSocket1() {
    final Socket result = socket1;
    socket1 = null;
    return result;
  }
}
