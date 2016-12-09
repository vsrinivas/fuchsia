// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of core;

class SocketWriteResult {
  final int status;
  final int bytesWritten;

  SocketWriteResult(this.status, this.bytesWritten);
  SocketWriteResult.fromList(List<int> resultList)
      : this(resultList[0], resultList[1]);

  String toString() {
    return 'SocketWriteResult('
        'status: ${getStringForStatus(status)}, bytesWritten: $bytesWritten)';
  }
}

class SocketReadResult {
  final int status;
  final int bytesRead;

  SocketReadResult(this.status, this.bytesRead);
  SocketReadResult.fromList(List<int> resultList)
      : this(resultList[0], resultList[1]);

  String toString() {
    return 'SocketReadResult('
        'status: ${getStringForStatus(status)}, bytesRead: $bytesRead)';
  }
}

class Socket {
  Handle handle;

  Socket(this.handle);

  SocketWriteResult write(ByteData data,
      [int offset = 0, int numBytes = -1, int flags = 0]) {
    if (handle == null)
      return SocketWriteResult(ERR_INVALID_ARGS, 0);

    int dataLengthInBytes = (data == null) ? 0 : data.lengthInBytes;

    // If numBytes has the default value, use the full length of the data.
    int dataNumBytes = (numBytes == -1) ? dataLengthInBytes : numBytes;
      if (offset < 0 || dataNumBytes < 0 ||
          offset + dataNumBytes > data.lengthInBytes)
      return SocketWriteResult(ERR_INVALID_ARGS, 0);

    List result = MxSocket.write(handle.h, data, offset, dataNumBytes, flags);

    if (result == null)
      return new SocketWriteResult(ERR_INVALID_ARGS, 0);

    assert((result is List) && (result.length == 2));
    SocketWriteResult writeResult = new SocketWriteResult.fromList(result);

    return writeResult;
  }

  SocketReadResult read(ByteData data,
      [int offset = 0, int numBytes = -1, int flags = 0]) {
    if (handle == null)
      return new SocketReadResult(ERR_INVALID_ARGS, 0);

    // If numBytes has the default value, use the full length of the data.
    int dataNumBytes;
    if (data == null) {
      dataNumBytes = 0;
    } else {
      dataNumBytes = (numBytes == -1) ? data.lengthInBytes : numBytes;
      if (offset < 0 || dataNumBytes < 0 ||
          offset + dataNumBytes > data.lengthInBytes)
        return new SocketReadResult(ERR_INVALID_ARGS, 0);
    }

    List result = MxSocket.read(
        handle.h, data, offset, dataNumBytes, flags);

    if (result == null)
      return new SocketReadResult(ERR_INVALID_ARGS, 0);

    assert((result is List) && (result.length == 2));
    SocketReadResult readResult = new SocketReadResult.fromList(result);

    return readResult;
  }

  void close() {
    handle.close();
    handle = null;
  }

  String toString() => 'Socket($handle)';
}

class SocketPair {
  SocketPair._({
    this.status: NO_ERROR,
    this.socket0,
    this.socket1,
  });

  factory SocketPair() {
    List result = MxSocket.create(0);
    assert((result is List) && (result.length == 3));

    return new SocketPair._(
      status: result[0],
      socket0: new Socket(new Handle(result[1])),
      socket1: new Socket(new Handle(result[2])),
    );
  }

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
