// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of core;

class DataPipeProducer {
  static const int FLAG_NONE = 0;
  static const int FLAG_ALL_OR_NONE = 1 << 0;

  final int elementBytes;
  Handle handle;
  int status;

  DataPipeProducer(this.handle,
      [this.status = MojoResult.kOk, this.elementBytes = 1]);

  int write(ByteData data, [int numBytes = -1, int flags = FLAG_NONE]) {
    if (handle == null) {
      status = MojoResult.kInvalidArgument;
      return 0;
    }

    int data_numBytes = (numBytes == -1) ? data.lengthInBytes : numBytes;
    List result =
        DataPipeNatives.MojoWriteData(handle.h, data, data_numBytes, flags);
    if (result == null) {
      status = MojoResult.kInvalidArgument;
      return 0;
    }

    assert((result is List) && (result.length == 2));
    status = result[0];
    return result[1];
  }

  // TODO(floitsch): remove bufferBytes.
  ByteData beginWrite(int bufferBytes, [int flags = FLAG_NONE]) {
    if (handle == null) {
      status = MojoResult.kInvalidArgument;
      return null;
    }

    List result = DataPipeNatives.MojoBeginWriteData(handle.h, flags);
    if (result == null) {
      status = MojoResult.kInvalidArgument;
      return null;
    }

    assert((result is List) && (result.length == 2));
    status = result[0];
    return result[1];
  }

  int endWrite(int bytesWritten) {
    if (handle == null) {
      status = MojoResult.kInvalidArgument;
      return status;
    }
    int result = DataPipeNatives.MojoEndWriteData(handle.h, bytesWritten);
    status = result;
    return status;
  }

  String toString() => "DataPipeProducer(handle: $handle, "
      "status: ${MojoResult.string(status)})";
}

class DataPipeConsumer {
  static const int FLAG_NONE = 0;
  static const int FLAG_ALL_OR_NONE = 1 << 0;
  static const int FLAG_DISCARD = 1 << 1;
  static const int FLAG_QUERY = 1 << 2;
  static const int FLAG_PEEK = 1 << 3;

  Handle handle;
  final int elementBytes;
  int status;

  DataPipeConsumer(this.handle,
      [this.status = MojoResult.kOk, this.elementBytes = 1]);

  int read(ByteData data, [int numBytes = -1, int flags = FLAG_NONE]) {
    if (handle == null) {
      status = MojoResult.kInvalidArgument;
      return 0;
    }

    int data_numBytes = (numBytes == -1) ? data.lengthInBytes : numBytes;
    List result =
        DataPipeNatives.MojoReadData(handle.h, data, data_numBytes, flags);
    if (result == null) {
      status = MojoResult.kInvalidArgument;
      return 0;
    }
    assert((result is List) && (result.length == 2));
    status = result[0];
    return result[1];
  }

  // TODO(floitsch): remove bufferBytes.
  ByteData beginRead([int bufferBytes = 0, int flags = FLAG_NONE]) {
    if (handle == null) {
      status = MojoResult.kInvalidArgument;
      return null;
    }

    List result = DataPipeNatives.MojoBeginReadData(handle.h, flags);
    if (result == null) {
      status = MojoResult.kInvalidArgument;
      return null;
    }

    assert((result is List) && (result.length == 2));
    status = result[0];
    return result[1];
  }

  int endRead(int bytesRead) {
    if (handle == null) {
      status = MojoResult.kInvalidArgument;
      return status;
    }
    int result = DataPipeNatives.MojoEndReadData(handle.h, bytesRead);
    status = result;
    return status;
  }

  int query() => read(null, 0, FLAG_QUERY);

  String toString() => "DataPipeConsumer(handle: $handle, "
      "status: ${MojoResult.string(status)}, "
      "available: ${query()})";
}

class DataPipe {
  static const int FLAG_NONE = 0;
  static const int DEFAULT_ELEMENT_SIZE = 1;
  static const int DEFAULT_CAPACITY = 0;

  DataPipeProducer producer;
  DataPipeConsumer consumer;
  int status;

  DataPipe._internal() : status = MojoResult.kOk;

  factory DataPipe(
      [int elementBytes = DEFAULT_ELEMENT_SIZE,
      int capacityBytes = DEFAULT_CAPACITY,
      int flags = FLAG_NONE]) {
    List result = DataPipeNatives.MojoCreateDataPipe(
        elementBytes, capacityBytes, flags);
    if (result == null) {
      return null;
    }
    assert((result is List) && (result.length == 3));
    Handle producerHandle = new Handle(result[1]);
    Handle consumerHandle = new Handle(result[2]);
    DataPipe pipe = new DataPipe._internal();
    pipe.producer =
        new DataPipeProducer(producerHandle, result[0], elementBytes);
    pipe.consumer =
        new DataPipeConsumer(consumerHandle, result[0], elementBytes);
    pipe.status = result[0];
    return pipe;
  }
}
