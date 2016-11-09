// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of core;

class ChannelReadResult {
  final int status;
  final int bytesRead;
  final int handlesRead;

  ChannelReadResult(this.status, this.bytesRead, this.handlesRead);
  ChannelReadResult.fromList(List<int> resultList)
      : this(resultList[0], resultList[1], resultList[2]);

  String toString() {
    return "ChannelReadResult("
        "status: ${getStringForStatus(status)}, bytesRead: $bytesRead, "
        "handlesRead: $handlesRead)";
  }
}

class ChannelQueryAndReadState {
  static final List _result = new List(5);

  List<Handle> _handles;

  int get status => _result[0];
  ByteData get data => _result[1];
  List<Handle> get handles => _handles;
  int get dataLength => _result[3];
  int get handlesLength => _result[4];

  ChannelQueryAndReadState();

  void error(int status) {
    _result[0] = status;
    _result[1] = null;
    _result[2] = null;
    _result[3] = null;
    _result[4] = null;
  }

  void queryAndRead(Handle handle, int flags) {
    MxChannel.queryAndRead(handle.h, flags, _result);

    if (handlesLength == 0) {
      _handles = null;
    } else {
      _handles = new List(handlesLength);
      for (int i = 0; i < handlesLength; i++) {
        _handles[i] = new Handle(_result[2][i]);
      }
    }
  }

  String toString() {
    return "ChannelQueryAndReadState("
        "status: ${getStringForStatus(status)}, dataLength: $dataLength, "
        "handlesLength: $handlesLength)";
  }
}

class Channel {
  static final ChannelQueryAndReadState _queryAndReadState =
      new ChannelQueryAndReadState();

  Handle handle;

  Channel(this.handle);

  int write(ByteData data,
      [int numBytes = -1, List<Handle> handles = null, int flags = 0]) {
    if (handle == null)
      return ERR_INVALID_ARGS;

    int dataLengthInBytes = (data == null) ? 0 : data.lengthInBytes;

    // If numBytes has the default value, use the full length of the data.
    int dataNumBytes = (numBytes == -1) ? dataLengthInBytes : numBytes;
    if (dataNumBytes > dataLengthInBytes)
      return ERR_INVALID_ARGS;

    // handles may be null, otherwise convert to ints.
    List<int> rawHandles;
    if (handles != null) {
      rawHandles = new List<int>(handles.length);
      for (int i = 0; i < handles.length; ++i) {
        rawHandles[i] = handles[i].h;
      }
    }

    return MxChannel.write(handle.h, data, dataNumBytes, rawHandles, flags);
  }

  ChannelReadResult read(ByteData data,
      [int numBytes = -1, List<Handle> handles = null, int flags = 0]) {
    if (handle == null)
      return new ChannelReadResult(ERR_INVALID_ARGS, 0, 0);

    // If numBytes has the default value, use the full length of the data.
    int dataNumBytes;
    if (data == null) {
      dataNumBytes = 0;
    } else {
      dataNumBytes = (numBytes == -1) ? data.lengthInBytes : numBytes;
      if (dataNumBytes > data.lengthInBytes)
        return new ChannelReadResult(ERR_INVALID_ARGS, 0, 0);
    }

    // handles may be null, otherwise make an int list for the handles.
    List<int> rawHandles;
    if (handles != null)
      rawHandles = new List<int>(handles.length);

    List result = MxChannel.read(
        handle.h, data, dataNumBytes, rawHandles, flags);

    if (result == null)
      return new ChannelReadResult(ERR_INVALID_ARGS, 0, 0);

    assert((result is List) && (result.length == 3));
    ChannelReadResult readResult = new ChannelReadResult.fromList(result);

    // Copy out the handles that were read.
    if (handles != null) {
      for (var i = 0; i < readResult.handlesRead; i++) {
        handles[i] = new Handle(rawHandles[i]);
      }
    }

    return readResult;
  }

  bool setDescription(String description) =>
      MxHandle.setDescription(handle.h, description);

  /// Warning: The object returned by this function, and the buffers inside of
  /// it are only valid until the next call to this function by the same
  /// isolate.
  ChannelQueryAndReadState queryAndRead([int flags = 0]) {
    if (handle == null) {
      _queryAndReadState.error(ERR_INVALID_ARGS);
    } else {
      _queryAndReadState.queryAndRead(handle, flags);
    }
    return _queryAndReadState;
  }

  void close() {
    handle.close();
    handle = null;
  }

  String toString() => "Channel(handle: $handle)";
}

class ChannelPair {
  List<Channel> channels;
  int status;

  ChannelPair._() : status = NO_ERROR;

  factory ChannelPair() {
    List result = MxChannel.create(0);
    if (result == null) {
      return null;
    }
    assert((result is List) && (result.length == 3));

    Handle end1 = new Handle(result[1]);
    Handle end2 = new Handle(result[2]);
    ChannelPair pipe = new ChannelPair._();
    pipe.channels = new List(2);
    pipe.channels[0] = new Channel(end1);
    pipe.channels[1] = new Channel(end2);
    pipe.status = result[0];
    return pipe;
  }
}
