// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of zircon_fakes;

// ignore_for_file: public_member_api_docs

// ignore: unused_element
class _Namespace {
  // ignore: unused_element
  // No public constructor - this only has static methods.
  _Namespace._();

  // Library private variable set by the embedder used to cache the
  // namespace (as an fdio_ns_t*).
  static int? _namespace; // ignore: unused_field
}

/// An exception representing an error returned as an zx_status_t.
class ZxStatusException implements Exception {
  final String? message;
  final int status;

  ZxStatusException(this.status, [this.message]);

  @override
  String toString() {
    if (message == null)
      return 'ZxStatusException: status = $status';
    else
      return 'ZxStatusException: status = $status, "$message"';
  }
}

class _Result {
  final int status;
  const _Result(this.status);
}

class HandleResult extends _Result {
  final Handle? handle;
  const HandleResult(final int status, [this.handle]) : super(status);
  @override
  String toString() => 'HandleResult(status=$status, handle=$handle)';
}

class HandlePairResult extends _Result {
  final Handle? first;
  final Handle? second;
  const HandlePairResult(final int status, [this.first, this.second])
      : super(status);
  @override
  String toString() =>
      'HandlePairResult(status=$status, first=$first, second=$second)';
}

class ReadResult extends _Result {
  final ByteData? bytes;
  final int? numBytes;
  final List<Handle>? handles;
  const ReadResult(final int status, [this.bytes, this.numBytes, this.handles])
      : super(status);
  Uint8List bytesAsUint8List() =>
      bytes!.buffer.asUint8List(bytes!.offsetInBytes, numBytes);
  String bytesAsUTF8String() => utf8.decode(bytesAsUint8List());
  @override
  String toString() =>
      'ReadResult(status=$status, bytes=$bytes, numBytes=$numBytes, handles=$handles)';
}

class WriteResult extends _Result {
  final int? numBytes;
  const WriteResult(final int status, [this.numBytes]) : super(status);
  @override
  String toString() => 'WriteResult(status=$status, numBytes=$numBytes)';
}

class GetSizeResult extends _Result {
  final int? size;
  const GetSizeResult(final int status, [this.size]) : super(status);
  @override
  String toString() => 'GetSizeResult(status=$status, size=$size)';
}

class FromFileResult extends _Result {
  final Handle? handle;
  final int? numBytes;
  const FromFileResult(final int status, [this.handle, this.numBytes])
      : super(status);
  @override
  String toString() =>
      'FromFileResult(status=$status, handle=$handle, numBytes=$numBytes)';
}

class MapResult extends _Result {
  final Uint8List? data;
  const MapResult(final int status, [this.data]) : super(status);
  @override
  String toString() => 'MapResult(status=$status, data=$data)';
}

class System {
  // No public constructor - this only has static methods.
  System._();

  // Channel operations.
  static HandlePairResult channelCreate([int options = 0]) {
    throw UnimplementedError(
        'System.channelCreate() is not implemented on this platform.');
  }

  static HandleResult channelFromFile(String path) {
    throw UnimplementedError(
        'System.channelFromFile() is not implemented on this platform.');
  }

  static int channelWrite(
      Handle? channel, ByteData data, List<Handle>? handles) {
    throw UnimplementedError(
        'System.channelWrite() is not implemented on this platform.');
  }

  static ReadResult channelQueryAndRead(Handle? channel) {
    throw UnimplementedError(
        'System.channelQueryAndRead() is not implemented on this platform.');
  }

  // Eventpair operations.
  static HandlePairResult eventpairCreate([int options = 0]) {
    throw UnimplementedError(
        'System.eventpairCreate() is not implemented on this platform.');
  }

  // Socket operations.
  static HandlePairResult socketCreate([int options = 0]) {
    throw UnimplementedError(
        'System.socketCreate() is not implemented on this platform.');
  }

  static WriteResult socketWrite(Handle? socket, ByteData data, int options) {
    throw UnimplementedError(
        'System.socketWrite() is not implemented on this platform.');
  }

  static ReadResult socketRead(Handle? socket, int size) {
    throw UnimplementedError(
        'System.socketRead() is not implemented on this platform.');
  }

  // Vmo operations.
  static HandleResult vmoCreate(int size, [int options = 0]) {
    throw UnimplementedError(
        'System.vmoCreate() is not implemented on this platform.');
  }

  static FromFileResult vmoFromFile(String path) {
    throw UnimplementedError(
        'System.vmoFromFile() is not implemented on this platform.');
  }

  static GetSizeResult vmoGetSize(Handle? vmo) {
    throw UnimplementedError(
        'System.vmoGetSize() is not implemented on this platform.');
  }

  static int vmoSetSize(Handle? vmo, int size) {
    throw UnimplementedError(
        'System.vmoSetSize() is not implemented on this platform.');
  }

  static int vmoWrite(Handle? vmo, int offset, ByteData bytes) {
    throw UnimplementedError(
        'System.vmoWrite() is not implemented on this platform.');
  }

  static ReadResult vmoRead(Handle? vmo, int offset, int size) {
    throw UnimplementedError(
        'System.vmoRead() is not implemented on this platform.');
  }

  static MapResult vmoMap(Handle? vmo) {
    throw UnimplementedError(
        'System.vmoMap() is not implemented on this platform.');
  }

  // Time operations.
  static int clockGet(int clockId) {
    throw UnimplementedError(
        'System.timeGet() is not implemented on this platform.');
  }

  // System operations.
  static int connectToService(String path, Handle channel) {
    throw UnimplementedError(
        'System.connectToService() is not implemented on this platform.');
  }
}
