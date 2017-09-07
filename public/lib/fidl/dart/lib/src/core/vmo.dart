// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of core;

class Vmo {
  Handle handle;

  Vmo(this.handle);

  GetSizeResult getSize() {
    if (handle == null) return const GetSizeResult(ZX.ERR_INVALID_ARGS);

    return System.vmoGetSize(handle);
  }

  int setSize(int size) {
    if (handle == null || size < 0) return ZX.ERR_INVALID_ARGS;

    return System.vmoSetSize(handle, size);
  }

  WriteResult write(ByteData data, [int vmoOffset = 0]) {
    if (handle == null) return const WriteResult(ZX.ERR_INVALID_ARGS);

    return System.vmoWrite(handle, vmoOffset, data);
  }

  ReadResult read(int numBytes, [int vmoOffset = 0]) {
    if (handle == null) return const ReadResult(ZX.ERR_INVALID_ARGS);

    return System.vmoRead(handle, vmoOffset, numBytes);
  }

  void close() {
    handle.close();
    handle = null;
  }

  @override
  String toString() => 'Vmo($handle)';
}
