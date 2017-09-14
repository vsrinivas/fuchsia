// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of zircon;

// ignore_for_file: native_function_body_in_non_sdk_code

/// An exception representing an error returned as an zx_status_t.
class MxStatusException extends Error {
  final int status;
  MxStatusException(this.status);
}

class _Result {
  final int status;
  const _Result(this.status);

  /// Throw an |MxStatusException| if the |status| is not |ZX_OK|.
  void checkStatus() {
    if (status != ZX.OK) {
      throw new MxStatusException(status);
    }
  }
}

class HandleResult extends _Result {
  final Handle handle;
  const HandleResult(final int status, [this.handle]) : super(status);
  @override
  String toString() => 'HandleResult(status=$status, handle=$handle)';
}

class HandlePairResult extends _Result {
  final Handle first;
  final Handle second;
  const HandlePairResult(final int status, [this.first, this.second])
      : super(status);
  @override
  String toString() => 'HandlePairResult(status=$status, first=$first, second=$second)';
}

class ReadResult extends _Result {
  final ByteData bytes;
  final int numBytes;
  final List<Handle> handles;
  const ReadResult(final int status, [this.bytes, this.numBytes, this.handles])
      : super(status);
  Uint8List bytesAsUint8List() =>
      bytes.buffer.asUint8List(bytes.offsetInBytes, numBytes);
  String bytesAsUTF8String() => UTF8.decode(bytesAsUint8List());
  @override
  String toString() => 'ReadResult(status=$status, bytes=$bytes, numBytes=$numBytes, handles=$handles)';
}

class WriteResult extends _Result {
  final int numBytes;
  const WriteResult(final int status, [this.numBytes]) : super(status);
  @override
  String toString() => 'WriteResult(status=$status, numBytes=$numBytes)';
}

class GetSizeResult extends _Result {
  final int size;
  const GetSizeResult(final int status, [this.size]) : super(status);
  @override
  String toString() => 'GetSizeResult(status=$status, size=$size)';
}

class System extends NativeFieldWrapperClass2 {
  // No public constructor - this only has static methods.
  System._();

  // Channel operations.
  static HandlePairResult channelCreate([int options = 0])
      native "System_ChannelCreate";
  static int channelWrite(Handle channel, ByteData data, List<Handle> handles)
      native "System_ChannelWrite";
  static ReadResult channelQueryAndRead(Handle channel)
      native "System_ChannelQueryAndRead";

  // Eventpair operations.
  static HandlePairResult eventpairCreate([int options = 0])
      native "System_EventpairCreate";

  // Socket operations.
  static HandlePairResult socketCreate([int options = ZX.SOCKET_STREAM])
      native "System_SocketCreate";
  static WriteResult socketWrite(Handle socket, ByteData data, int options)
      native "System_SocketWrite";
  static ReadResult socketRead(Handle socket, int size)
      native "System_SocketRead";

  // Vmo operations.
  static HandleResult vmoCreate(int size, [int options = 0])
      native "System_VmoCreate";
  static GetSizeResult vmoGetSize(Handle vmo) native "System_VmoGetSize";
  static int vmoSetSize(Handle vmo, int size) native "System_VmoSetSize";
  static WriteResult vmoWrite(Handle vmo, int offset, ByteData bytes)
      native "System_VmoWrite";
  static ReadResult vmoRead(Handle vmo, int offset, int size)
      native "System_VmoRead";

  // Time operations.
  static int timeGet(int clockId) native "System_TimeGet";
}
