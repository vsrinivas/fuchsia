// Copyright 2016 The Fuchsia Authors. All rights reserved.
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
    return 'ChannelReadResult('
        'status: ${getStringForStatus(status)}, bytesRead: $bytesRead, '
        'handlesRead: $handlesRead)';
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
    return 'ChannelQueryAndReadState('
        'status: ${getStringForStatus(status)}, dataLength: $dataLength, '
        'handlesLength: $handlesLength)';
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

    List<int> rawHandles;
    if (handles != null) {
      rawHandles = new List<int>(handles.length);
      for (int i = 0; i < handles.length; ++i)
        rawHandles[i] = handles[i].release();
    }

    int status = MxChannel.write(handle.h, data, dataNumBytes, rawHandles, flags);

    if (status != NO_ERROR) {
      for (int i = 0; i < rawHandles.length; ++i)
        MxHandle.close(rawHandles[i]);
    }

    return status;
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
      for (var i = 0; i < readResult.handlesRead; i++)
        handles[i] = new Handle(rawHandles[i]);
    }

    return readResult;
  }

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

  String toString() => 'Channel($handle)';
}

class ChannelPair {
  ChannelPair._({
    this.status: NO_ERROR,
    this.channel0,
    this.channel1,
  });

  factory ChannelPair() {
    List result = MxChannel.create(0);
    assert((result is List) && (result.length == 3));

    return new ChannelPair._(
      status: result[0],
      channel0: new Channel(new Handle(result[1])),
      channel1: new Channel(new Handle(result[2])),
    );
  }

  final int status;
  Channel channel0;
  Channel channel1;

  Channel passChannel0() {
    final Channel result = channel0;
    channel0 = null;
    return result;
  }

  Channel passChannel1() {
    final Channel result = channel1;
    channel1 = null;
    return result;
  }
}
