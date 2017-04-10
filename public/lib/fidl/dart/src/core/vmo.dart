// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of core;

class VmoGetSizeResult {
  final int status;
  final int size;

  VmoGetSizeResult(this.status, this.size);
  VmoGetSizeResult.fromList(List<int> resultList)
      : this(resultList[0], resultList[1]);

  String toString() {
    return 'VmoGetSizeResult('
        'status: ${getStringForStatus(status)}, size: $size)';
  }
}

class VmoWriteResult {
  final int status;
  final int bytesWritten;

  VmoWriteResult(this.status, this.bytesWritten);
  VmoWriteResult.fromList(List<int> resultList)
      : this(resultList[0], resultList[1]);

  String toString() {
    return 'VmoWriteResult('
        'status: ${getStringForStatus(status)}, bytesWritten: $bytesWritten)';
  }
}

class VmoReadResult {
  final int status;
  final int bytesRead;

  VmoReadResult(this.status, this.bytesRead);
  VmoReadResult.fromList(List<int> resultList)
      : this(resultList[0], resultList[1]);

  String toString() {
    return 'VmoReadResult('
        'status: ${getStringForStatus(status)}, bytesRead: $bytesRead)';
  }
}

class Vmo {
  Handle handle;

  Vmo(this.handle);

  VmoGetSizeResult getSize() {
    if (handle == null)
      return VmoGetSizeResult(ERR_INVALID_ARGS, 0);

    List result = MxVmo.getSize(handle.h);

    if (result == null)
      return new VmoGetSizeResult(ERR_INVALID_ARGS, 0);

    assert((result is List) && (result.length == 2));
    VmoGetSizeResult sizeResult = new VmoGetSizeResult.fromList(result);

    return sizeResult;
  }

  int setSize(int size) {
    if (handle == null || size < 0)
      return VmoGetSizeResult(ERR_INVALID_ARGS, 0);

    return MxVmo.setSize(handle.h, size);
  }

  VmoWriteResult write(ByteData data,
      [int vmoOffset = 0, int dataOffset = 0, int numBytes = -1]) {
    if (handle == null)
      return VmoWriteResult(ERR_INVALID_ARGS, 0);

    int dataLengthInBytes = (data == null) ? 0 : data.lengthInBytes;

    // If numBytes has the default value, use the full length of the data.
    int dataNumBytes = (numBytes == -1) ? dataLengthInBytes : numBytes;
      if (vmoOffset < 0 || dataOffset < 0 || dataNumBytes < 0 ||
          dataOffset + dataNumBytes > data.lengthInBytes)
      return VmoWriteResult(ERR_INVALID_ARGS, 0);

    List result = MxVmo.write(handle.h, vmoOffset, data, dataOffset,
        dataNumBytes);

    if (result == null)
      return new VmoWriteResult(ERR_INVALID_ARGS, 0);

    assert((result is List) && (result.length == 2));
    VmoWriteResult writeResult = new VmoWriteResult.fromList(result);

    return writeResult;
  }

  VmoReadResult read(ByteData data,
      [int vmoOffset = 0, int dataOffset = 0, int numBytes = -1]) {
    if (handle == null)
      return new VmoReadResult(ERR_INVALID_ARGS, 0);

    // If numBytes has the default value, use the full length of the data.
    int dataNumBytes;
    if (data == null) {
      dataNumBytes = 0;
    } else {
      dataNumBytes = (numBytes == -1) ? data.lengthInBytes : numBytes;
      if (dataOffset < 0 || dataNumBytes < 0 ||
          dataOffset + dataNumBytes > data.lengthInBytes)
        return new VmoReadResult(ERR_INVALID_ARGS, 0);
    }

    List result = MxVmo.read(
        handle.h, vmoOffset, data, dataOffset, dataNumBytes);

    if (result == null)
      return new VmoReadResult(ERR_INVALID_ARGS, 0);

    assert((result is List) && (result.length == 2));
    VmoReadResult readResult = new VmoReadResult.fromList(result);

    return readResult;
  }

  void close() {
    handle.close();
    handle = null;
  }

  String toString() => 'Vmo($handle)';
}

class VmoBuilder {
  VmoBuilder._({
    this.status: NO_ERROR,
    this.vmo,
  });

  factory VmoBuilder(int size) {
    List result = MxVmo.create(size, 0);
    assert((result is List) && (result.length == 2));

    return new VmoBuilder._(
      status: result[0],
      vmo: new Vmo(new Handle(result[1])),
    );
  }

  final int status;
  Vmo vmo;

  Vmo passVmo() {
    final Vmo result = vmo;
    vmo = null;
    return result;
  }
}
