// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:math';
import 'dart:typed_data';

import 'package:fidl/fidl.dart' as fidl;
import 'package:fidl_fuchsia_io/fidl_async.dart';
import 'package:meta/meta.dart';
import 'package:zircon/zircon.dart';

import 'internal/_flags.dart';
import 'vnode.dart';

// ignore_for_file: public_member_api_docs, unnecessary_null_comparison, unused_import

typedef WriteFn = int Function(Uint8List);
typedef WriteFnStr = int Function(String);
typedef ReadFn = Uint8List Function();
typedef ReadFnStr = String? Function();

/// A [PseudoFile] is a file-like object whose content is generated and modified
/// dynamically on-the-fly by invoking handler functions rather than being
/// directly persisted as a sequence of bytes.
///
/// This class is designed to allow programs to publish read-only,
/// or read-write properties such as configuration options, debug flags,
/// and dumps of internal state which may change dynamically.
///
/// Although [PseudoFile] usually contain text, they can also be used for binary
/// data.
///
/// Read callback, is called when the connection to the file is opened and
/// pre-populates a buffer that will be used to when serving this file content
/// over this particular connection.
///
/// Write callback, if any, is called when the connection is closed if the file
/// content was ever modified while the connection was open.
/// Modifications are: [fidl_fuchsia_io.File#write()] calls or opening a file
/// for writing with the `OpenFlags.truncate` flag set.
class PseudoFile extends Vnode {
  final int _capacity;
  ReadFn? _readFn;
  WriteFn? _writeFn;
  bool _isClosed = false;
  final List<_FileConnection> _connections = [];

  /// Creates a new read-only [PseudoFile] backed by the specified read handler.
  ///
  /// The handler is called every time a read operation is performed on the file.  It is only allowed
  /// to read at offset 0, and all of the content returned by the handler is returned by the read
  /// operation.  Subsequent reads act the same - there is no seek position, nor ability to read
  /// content in chunks.
  PseudoFile.readOnly(this._readFn)
      : _capacity = 0,
        assert(_readFn != null);

  /// See [#readOnly()].  Wraps the callback, allowing it to return a String instead of a Uint8List,
  /// but otherwise behaves identical to [#readOnly()].
  PseudoFile.readOnlyStr(ReadFnStr fn)
      : _capacity = 0,
        assert(fn != null) {
    _readFn = _getReadFn(fn);
  }

  /// Creates new [PseudoFile] backed by the specified read and write handlers.
  ///
  /// The read handler is called every time a read operation is performed on the file.  It is only
  /// allowed to read at offset 0, and all of the content returned by the handler is returned by the
  /// read operation.  Subsequent reads act the same - there is no seek position, nor ability to read
  /// content in chunks.
  ///
  /// The write handler is called every time a write operation is performed on the file.  It is only
  /// allowed to write at offset 0, and all of the new content should be provided to a single write
  /// operation.  Subsequent writes act the same - there is no seek position, nor ability to write
  /// content in chunks.
  PseudoFile.readWrite(this._capacity, this._readFn, this._writeFn)
      : assert(_writeFn != null),
        assert(_readFn != null),
        assert(_capacity > 0);

  /// See [#readWrite()].  Wraps the read callback, allowing it to return a [String] instead of a
  /// [Uint8List].  Wraps the write callback, only allowing valid UTF-8 content to be written into
  /// the file.  Written bytes are converted into a string instance, and the passed to the handler.
  /// In every other aspect behaves just like [#readWrite()].
  PseudoFile.readWriteStr(this._capacity, ReadFnStr rfn, WriteFnStr wfn)
      : assert(_capacity > 0),
        assert(rfn != null),
        assert(wfn != null) {
    _readFn = _getReadFn(rfn);
    _writeFn = _getWriteFn(wfn);
  }

  /// Connects to this instance of [PseudoFile] and serves [fidl_fuchsia_io.File] over fidl.
  @override
  int connect(OpenFlags flags, int mode, fidl.InterfaceRequest<Node> request,
      [OpenFlags? parentFlags]) {
    if (_isClosed) {
      sendErrorEvent(flags, ZX.ERR_NOT_SUPPORTED, request);
      return ZX.ERR_NOT_SUPPORTED;
    }
    // There should be no MODE_TYPE_* flags set, except for, possibly,
    // MODE_TYPE_FILE when the target is a pseudo file.
    if ((mode & ~modeProtectionMask) & ~modeTypeFile != 0) {
      sendErrorEvent(flags, ZX.ERR_INVALID_ARGS, request);
      return ZX.ERR_INVALID_ARGS;
    }

    final connectFlags = filterForNodeReference(flags);
    final status =
        _validateFlags(parentFlags ?? Flags.fsRightsDefault(), connectFlags);
    if (status != ZX.OK) {
      sendErrorEvent(connectFlags, status, request);
      return status;
    }

    final connection = _FileConnection(
        capacity: _capacity,
        flags: connectFlags,
        file: this,
        mode: mode,
        request: fidl.InterfaceRequest<File>(request.passChannel()));

    // [connection] will also send on_open success event.
    _connections.add(connection);
    return ZX.OK;
  }

  @override
  int inodeNumber() {
    return inoUnknown;
  }

  @override
  DirentType type() {
    return DirentType.file;
  }

  NodeInfoDeprecated describeDeprecated() {
    return NodeInfoDeprecated.withFile(FileObject(event: null));
  }

  FileInfo describe() {
    return FileInfo();
  }

  ConnectionInfo getConnectionInfo() {
    return ConnectionInfo();
  }

  Vmo getBackingMemory(VmoFlags flags) {
    throw fidl.MethodException(ZX.ERR_NOT_SUPPORTED);
  }

  ReadFn _getReadFn(ReadFnStr fn) {
    return () => Uint8List.fromList(fn()!.codeUnits);
  }

  WriteFn _getWriteFn(WriteFnStr fn) {
    return (Uint8List buffer) => fn(String.fromCharCodes(buffer));
  }

  void _onClose(_FileConnection obj) {
    final result = _connections.remove(obj);
    scheduleMicrotask(() {
      obj.closeBinding();
    });
    assert(result);
  }

  int _validateFlags(OpenFlags parentFlags, OpenFlags flags) {
    if (flags & OpenFlags.directory != OpenFlags.$none) {
      return ZX.ERR_NOT_DIR;
    }
    var allowedFlags = OpenFlags.describe |
        OpenFlags.nodeReference |
        OpenFlags.posixWritable |
        OpenFlags.posixExecutable |
        OpenFlags.cloneSameRights;
    if (_readFn != null) {
      allowedFlags |= OpenFlags.rightReadable;
    }
    if (_writeFn != null) {
      allowedFlags |= OpenFlags.rightWritable | OpenFlags.truncate;
    }

    // allowedFlags takes precedence over prohibited_flags.
    const prohibitedFlags = OpenFlags.append;

    final flagsDependentOnParentFlags = [
      OpenFlags.rightReadable,
      OpenFlags.rightWritable
    ];
    for (final flag in flagsDependentOnParentFlags) {
      if (flags & flag != OpenFlags.$none &&
          parentFlags & flag == OpenFlags.$none) {
        return ZX.ERR_ACCESS_DENIED;
      }
    }

    if (flags & prohibitedFlags != OpenFlags.$none) {
      return ZX.ERR_INVALID_ARGS;
    }
    if (flags & ~allowedFlags != OpenFlags.$none) {
      return ZX.ERR_NOT_SUPPORTED;
    }
    return ZX.OK;
  }

  @override
  void close() {
    _isClosed = true;
    // schedule a task because if user closes this as soon as
    // they open a connection, dart fidl binding throws exception due to
    // event on this fidl.
    scheduleMicrotask(() {
      for (final c in _connections) {
        c.closeBinding();
      }
      _connections.clear();
    });
  }
}

/// Implementation of fuchsia.io.File for pseudo file.
///
/// This class should not be used directly, but by [fuchsia_vfs.PseudoFile].
class _FileConnection extends File {
  final FileBinding _binding = FileBinding();

  /// open file connection flags
  final OpenFlags flags;

  /// open file mode
  final int mode;

  /// seek position in file.
  int seekPos = 0;

  /// file's maximum capacity.
  int capacity;

  /// current length of file.
  int _currentLen = 0;

  // TODO(fxbug.dev/4143): Implement a grow-able buffer.
  /// buffer which stores file content
  Uint8List _buffer = Uint8List(0);

  /// true if client wrote to this file.
  bool _wasWritten = false;

  /// Reference to PsuedoFile's Vnode.
  PseudoFile file;

  bool _isClosed = false;

  /// Constructor to init _FileConnection
  _FileConnection({
    required this.flags,
    required this.mode,
    required this.capacity,
    required this.file,
    required fidl.InterfaceRequest<File> request,
  }) : assert(file != null) {
    if (file._writeFn != null) {
      _buffer = Uint8List(capacity);
    }

    if (flags & OpenFlags.truncate != OpenFlags.$none) {
      // don't call read handler on truncate.
      _wasWritten = true;
    } else {
      final readBuf = file._readFn!();
      _currentLen = readBuf.lengthInBytes;
      if (_currentLen > capacity) {
        capacity = _currentLen;
        _buffer = Uint8List(capacity);
      }
      _buffer.setRange(0, _currentLen, readBuf);
    }
    _binding.bind(this, request);
    _binding.whenClosed.then((_) => close());
  }

  void closeBinding() {
    _binding.close();
    _isClosed = true;
  }

  @override
  Stream<File$OnOpen$Response> get onOpen {
    File$OnOpen$Response d;
    if ((flags & OpenFlags.describe) == OpenFlags.$none) {
      d = File$OnOpen$Response(ZX.ERR_NOT_FILE, null);
    } else {
      NodeInfoDeprecated nodeInfo = file.describeDeprecated();
      d = File$OnOpen$Response(ZX.OK, nodeInfo);
    }
    return Stream.fromIterable([d]);
  }

  // TODO(https://fxbug.dev/77623): Switch from onOpen to onRepresentation when
  // clients are ready.
  @override
  Stream<Representation> get onRepresentation async* {}

  @override
  Future<void> advisoryLock(AdvisoryLockRequest request) async {
    throw fidl.MethodException(ZX.ERR_NOT_SUPPORTED);
  }

  @override
  Future<void> clone(
      OpenFlags flags, fidl.InterfaceRequest<Node> object) async {
    if (!Flags.inputPrecondition(flags)) {
      file.sendErrorEvent(flags, ZX.ERR_INVALID_ARGS, object);
      return;
    }
    if (Flags.shouldCloneWithSameRights(flags)) {
      if ((flags & openRights) != OpenFlags.$none) {
        file.sendErrorEvent(flags, ZX.ERR_INVALID_ARGS, object);
        return;
      }
    }

    // If SAME_RIGHTS is requested, cloned connection will inherit the same
    // rights as those from the originating connection.
    var newFlags = flags;
    if (Flags.shouldCloneWithSameRights(flags)) {
      newFlags &= (~openRights);
      newFlags |= (this.flags & openRights);
      newFlags &= ~OpenFlags.cloneSameRights;
    }

    if (!Flags.stricterOrSameRights(newFlags, this.flags)) {
      file.sendErrorEvent(flags, ZX.ERR_ACCESS_DENIED, object);
      return;
    }

    file.connect(newFlags, mode, object, this.flags);
  }

  @override
  Future<void> close() async {
    if (_isClosed) {
      return;
    }
    final status = () {
      if (file._writeFn != null && _wasWritten) {
        return file._writeFn!(_buffer.buffer.asUint8List(0, _currentLen));
      }
      return ZX.OK;
    }();
    // no more read/write operations should be possible
    scheduleMicrotask(() {
      file._onClose(this);
    });
    _isClosed = true;
    if (status != ZX.OK) {
      throw fidl.MethodException(status);
    }
  }

  @override
  Future<Uint8List> query() async {
    return Utf8Encoder().convert(fileProtocolName);
  }

  @override
  Future<NodeInfoDeprecated> describeDeprecated() async =>
      file.describeDeprecated();

  @override
  Future<FileInfo> describe() async => file.describe();

  @override
  Future<ConnectionInfo> getConnectionInfo() async => file.getConnectionInfo();

  @override
  Future<File$GetAttr$Response> getAttr() async {
    return File$GetAttr$Response(
        ZX.OK,
        NodeAttributes(
            mode: modeTypeFile | modeProtectionMask,
            id: inoUnknown,
            contentSize: 0,
            storageSize: 0,
            linkCount: 1,
            creationTime: 0,
            modificationTime: 0));
  }

  @override
  Future<File$GetFlags$Response> getFlags() async {
    return File$GetFlags$Response(ZX.OK, flags);
  }

  @override
  Future<Vmo> getBackingMemory(VmoFlags flags) async =>
      file.getBackingMemory(flags);

  @override
  Future<Uint8List> read(int count) async {
    final response = await readAt(count, seekPos);
    seekPos += response.length;
    return response;
  }

  @override
  Future<Uint8List> readAt(int count, int offset) async {
    if ((flags & OpenFlags.rightReadable) == OpenFlags.$none) {
      throw fidl.MethodException(ZX.ERR_ACCESS_DENIED);
    }
    if (file._readFn == null) {
      throw fidl.MethodException(ZX.ERR_NOT_SUPPORTED);
    }
    if (offset > _currentLen) {
      throw fidl.MethodException(ZX.ERR_OUT_OF_RANGE);
    }
    final c = min(count, _currentLen - offset);
    return Uint8List.view(_buffer.buffer, offset, c);
  }

  @override
  Future<int> seek(SeekOrigin origin, int offset) async {
    final offsetFromStart = offset +
        (() {
          switch (origin) {
            case SeekOrigin.start:
              return 0;
            case SeekOrigin.current:
              return seekPos;
            case SeekOrigin.end:
              return _currentLen - 1;
            default:
              throw fidl.MethodException(ZX.ERR_INVALID_ARGS);
          }
        })();
    if (offsetFromStart > _currentLen || offsetFromStart < 0) {
      throw fidl.MethodException(ZX.ERR_OUT_OF_RANGE);
    }
    seekPos = offsetFromStart;
    return offsetFromStart;
  }

  @override
  Future<int> setAttr(
      NodeAttributeFlags flags, NodeAttributes attributes) async {
    return ZX.ERR_NOT_SUPPORTED;
  }

  @override
  Future<int> setFlags(OpenFlags flags) async {
    return ZX.ERR_NOT_SUPPORTED;
  }

  @override
  Future<void> sync() async {
    throw fidl.MethodException(ZX.ERR_NOT_SUPPORTED);
  }

  @override
  Future<void> resize(int length) async {
    if ((flags & OpenFlags.rightWritable) == OpenFlags.$none) {
      throw fidl.MethodException(ZX.ERR_ACCESS_DENIED);
    }
    if (file._writeFn == null) {
      throw fidl.MethodException(ZX.ERR_NOT_SUPPORTED);
    }
    if (length > _currentLen) {
      throw fidl.MethodException(ZX.ERR_OUT_OF_RANGE);
    }

    _currentLen = length;
    seekPos = min(seekPos, _currentLen);
    _wasWritten = true;
  }

  @override
  Future<int> write(Uint8List data) async {
    final actual = _handleWrite(seekPos, data);
    seekPos += actual;
    return actual;
  }

  @override
  Future<int> writeAt(Uint8List data, int offset) async {
    return _handleWrite(offset, data);
  }

  int _handleWrite(int offset, Uint8List data) {
    if ((flags & OpenFlags.rightWritable) == OpenFlags.$none) {
      throw fidl.MethodException(ZX.ERR_ACCESS_DENIED);
    }
    if (file._writeFn == null) {
      throw fidl.MethodException(ZX.ERR_NOT_SUPPORTED);
    }
    if (offset >= capacity) {
      throw fidl.MethodException(ZX.ERR_OUT_OF_RANGE);
    }
    if (offset > _currentLen) {
      throw fidl.MethodException(ZX.ERR_OUT_OF_RANGE);
    }

    final actual = min(data.length, capacity - offset);
    _buffer.setRange(offset, offset + actual, data.getRange(0, actual));
    _wasWritten = true;
    _currentLen = offset + actual;
    return actual;
  }

  @override
  Future<File$QueryFilesystem$Response> queryFilesystem() async {
    return File$QueryFilesystem$Response(ZX.ERR_NOT_SUPPORTED, null);
  }
}
