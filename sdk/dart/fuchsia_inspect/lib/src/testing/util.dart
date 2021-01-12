// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports

import 'dart:typed_data';

import 'package:zircon/zircon.dart';
import 'package:fuchsia_inspect/src/vmo/vmo_holder.dart';

/// A VmoHolder that simply wraps some ByteData.
class FakeVmoHolder implements VmoHolder {
  /// The memory contents of this "VMO".
  final ByteData bytes;

  @override
  Vmo get vmo => Vmo(null);

  /// Size of the "VMO".
  @override
  final int size;

  /// Creates a new [FakeVmoHolder] of the given size.
  FakeVmoHolder(this.size) : bytes = Uint8List(size).buffer.asByteData();

  /// Wraps a [FakeVmoHolder] around the given data.
  FakeVmoHolder.usingData(this.bytes) : size = bytes.lengthInBytes;

  /// Snapshots and loads a real VMO in this holder.
  factory FakeVmoHolder.fromVmo(Vmo vmo, {int retries = 1024}) {
    while (retries > 0) {
      retries--;

      // Spin until we find an even generation count (no concurrent update).
      var headerData = vmo.read(16);
      if (headerData.status != 0 ||
          headerData.bytes.getInt64(8, Endian.little) % 2 != 0) {
        continue;
      }

      // Read the entire VMO.
      var sizeResult = vmo.getSize();
      if (sizeResult.status != 0) {
        continue;
      }

      var size = sizeResult.size;
      var fullData = vmo.read(size);
      if (fullData.status != 0) {
        continue;
      }

      // Read the header again, and check that the generation counts match.
      // This means we have a consistent snapshot.
      var headerDataAfter = vmo.read(16);
      if (headerDataAfter.status != 0 ||
          headerData.bytes.getInt64(8, Endian.little) !=
              headerDataAfter.bytes.getInt64(8, Endian.little)) {
        continue;
      }

      return FakeVmoHolder.usingData(fullData.bytes);
    }

    return null;
  }

  @override
  void beginWork() {}

  @override
  void commit() {}

  /// Writes to the "VMO".
  @override
  void write(int offset, ByteData data) {
    bytes.buffer.asUint8List().setAll(offset,
        data.buffer.asUint8List(data.offsetInBytes, data.lengthInBytes));
  }

  /// Reads from the "VMO".
  @override
  ByteData read(int offset, int size) {
    var reading = ByteData(size);
    reading.buffer
        .asUint8List()
        .setAll(0, bytes.buffer.asUint8List(offset, size));
    return reading;
  }

  /// Writes int64 to VMO.
  @override
  void writeInt64(int offset, int value) =>
      bytes.setInt64(offset, value, Endian.little);

  /// Writes int64 directly to VMO for immediate visibility.
  @override
  void writeInt64Direct(int offset, int value) => writeInt64(offset, value);

  /// Reads int64 from VMO.
  @override
  int readInt64(int offset) => bytes.getInt64(offset, Endian.little);
}
