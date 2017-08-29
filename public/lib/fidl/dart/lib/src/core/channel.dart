// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of core;

class Channel {
  Handle handle;

  Channel(this.handle);

  int write(ByteData data, [List<Handle> handles]) {
    if (handle == null) return ERR_INVALID_ARGS;

    return System.channelWrite(handle, data, handles);
  }

  ReadResult queryAndRead() {
    if (handle == null) {
      return const ReadResult(ERR_INVALID_ARGS);
    }
    return System.channelQueryAndRead(handle);
  }

  void close() {
    handle.close();
    handle = null;
  }

  @override
  String toString() => 'Channel($handle)';
}

class ChannelPair {
  factory ChannelPair() {
    HandlePairResult result = System.channelCreate();

    if (result.status == NO_ERROR) {
      return new ChannelPair._(
        status: result.status,
        channel0: new Channel(result.first),
        channel1: new Channel(result.second),
      );
    } else {
      return new ChannelPair._(
        status: result.status,
        channel0: null,
        channel1: null,
      );
    }
  }

  ChannelPair._({
    this.status: NO_ERROR,
    this.channel0,
    this.channel1,
  });

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
