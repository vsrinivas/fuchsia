// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of zircon;

// ignore_for_file: constant_identifier_names

class Channel extends _HandleWrapper<Channel> {
  Channel(Handle handle) : super(handle);

  // Signals
  static const int READABLE = ZX.CHANNEL_READABLE;
  static const int WRITABLE = ZX.CHANNEL_WRITABLE;
  static const int PEER_CLOSED = ZX.CHANNEL_PEER_CLOSED;

  // Read options
  static const int READ_MAY_DISCARD = ZX.CHANNEL_READ_MAY_DISCARD;

  // Limits
  static const int MAX_MSG_BYTES = ZX.CHANNEL_MAX_MSG_BYTES;
  static const int MAX_MSG_HANDLES = ZX.CHANNEL_MAX_MSG_HANDLES;

  int write(ByteData data, [List<Handle> handles]) {
    if (handle == null) return ZX.ERR_INVALID_ARGS;

    return System.channelWrite(handle, data, handles);
  }

  ReadResult queryAndRead() {
    if (handle == null) {
      return const ReadResult(ZX.ERR_INVALID_ARGS);
    }
    return System.channelQueryAndRead(handle);
  }
}

class ChannelPair extends _HandleWrapperPair<Channel> {
  factory ChannelPair() {
    final HandlePairResult result = System.channelCreate();
    if (result.status == ZX.OK) {
      return new ChannelPair._(
          result.status, new Channel(result.first), new Channel(result.second));
    } else {
      return new ChannelPair._(result.status, null, null);
    }
  }

  ChannelPair._(int status, Channel first, Channel second)
      : super._(status, first, second);
}
