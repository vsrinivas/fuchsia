// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

import 'package:zircon/zircon.dart';

/// Holder for a VMO with read/write capability.
class VmoHolder {
  /// Size of the VMO in bytes
  final int size;
  Vmo? _vmo;

  /// Dart currently requires a syscall to write to a VMO, but provides a way
  /// to map it read-only. [_shadow] will hold that map to avoid syscall
  /// overhead on reads.
  late Uint8List _shadow;

  /// Creates and holds a VMO of desired size.
  VmoHolder(this.size) {
    HandleResult result = System.vmoCreate(size);
    if (result.status != ZX.OK) {
      throw ZxStatusException(result.status, getStringForStatus(result.status));
    }
    _vmo = Vmo(result.handle);
    _shadow = _vmo!.map();
  }

  /// The raw VMO
  Vmo? get vmo => _vmo;

  /// Starts an update.
  void beginWork() {}

  //// Finishes an update
  void commit() {}

  /// Writes data to VMO at byte offset (not index).
  ///
  /// Data will be visible to other processes by end of next commit().
  void write(int offset, ByteData data) {
    int status = _vmo!.write(data, offset);
    if (status != ZX.OK) {
      throw ZxStatusException(status, getStringForStatus(status));
    }
  }

  /// Reads data from VMO at byte offset (not index).
  ByteData read(int offset, int size) =>
      ByteData.view(_shadow.buffer, offset, size);

  /// Writes int64 to VMO.
  ///
  /// Data will be visible to other processes by end of next commit().
  void writeInt64(int offset, int value) {
    var data = ByteData(8)..setInt64(0, value, Endian.little);
    write(offset, data);
  }

  /// Writes int64 directly to VMO for immediate visibility.
  void writeInt64Direct(int offset, int value) {
    var data = ByteData(8)..setInt64(0, value, Endian.little);
    write(offset, data);
  }

  /// Reads int64 from VMO.
  int readInt64(int offset) {
    ByteData data = read(offset, 8);
    return data.getInt64(0, Endian.little);
  }
}
