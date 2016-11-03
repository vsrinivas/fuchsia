// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of core;

class SharedBufferInformation {
  final int flags;
  final int sizeInBytes;

  SharedBufferInformation(this.flags, this.sizeInBytes);
}


class SharedBuffer {
  static const int createFlagNone = 0;
  static const int duplicateFlagNone = 0;
  static const int mapFlagNone = 0;

  Handle _handle;
  int _status = MojoResult.kOk;

  Handle get handle => _handle;
  int get status => _status;

  SharedBuffer(this._handle, [this._status = MojoResult.kOk]);

  factory SharedBuffer.create(int numBytes, [int flags = createFlagNone]) {
    List result = SharedBufferNatives.Create(numBytes, flags);
    if (result == null) {
      return null;
    }
    if (result[0] != MojoResult.kOk) {
      return null;
    }

    SharedBuffer buf =
        new SharedBuffer(new Handle(result[1]), result[0]);
    return buf;
  }

  factory SharedBuffer.duplicate(SharedBuffer msb,
      [int flags = duplicateFlagNone]) {
    List result = SharedBufferNatives.Duplicate(msb.handle.h, flags);
    if (result == null) {
      return null;
    }
    if (result[0] != MojoResult.kOk) {
      return null;
    }

    SharedBuffer dupe =
        new SharedBuffer(new Handle(result[1]), result[0]);
    return dupe;
  }

  SharedBufferInformation get information {
    if (handle == null) {
      _status = MojoResult.kInvalidArgument;
      return null;
    }

    List result = SharedBufferNatives.GetInformation(handle.h);

    if (result[0] != MojoResult.kOk) {
      _status = result[0];
      return null;
    }

    return new SharedBufferInformation(result[1], result[2]);
  }

  int close() {
    if (handle == null) {
      _status = MojoResult.kInvalidArgument;
      return _status;
    }
    int r = handle.close();
    _status = r;
    return _status;
  }

  ByteData map(int offset, int numBytes, [int flags = mapFlagNone]) {
    if (handle == null) {
      _status = MojoResult.kInvalidArgument;
      return null;
    }
    List result =
        SharedBufferNatives.Map(handle.h, offset, numBytes, flags);
    if (result == null) {
      _status = MojoResult.kInvalidArgument;
      return null;
    }
    _status = result[0];
    return result[1];
  }
}
