// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of zircon;

class ChannelReaderError {
  final Object error;
  final StackTrace stacktrace;

  ChannelReaderError(this.error, this.stacktrace);

  @override
  String toString() => error.toString();
}

typedef void ChannelReaderReadableHandler();
typedef void ChannelReaderErrorHandler(ChannelReaderError error);

class ChannelReader {
  Channel get channel => _channel;
  Channel _channel;

  bool get isBound => _channel != null;

  HandleWaiter _waiter;

  ChannelReaderReadableHandler onReadable;
  ChannelReaderErrorHandler onError;

  void bind(Channel channel) {
    if (isBound) throw new ZirconApiError('ChannelReader is already bound.');
    _channel = channel;
    _asyncWait();
  }

  Channel unbind() {
    if (!isBound) throw new ZirconApiError("ChannelReader is not bound");
    _waiter?.cancel();
    final Channel result = _channel;
    _channel = null;
    return result;
  }

  void close() {
    if (!isBound) return;
    _waiter.cancel();
    _channel.close();
    _channel = null;
  }

  void _asyncWait() {
    _waiter = _channel.handle.asyncWait(
        Channel.READABLE | Channel.PEER_CLOSED, _handleWaitComplete);
  }

  void _errorSoon(ChannelReaderError error) {
    if (onError == null) return;
    scheduleMicrotask(() {
      // We need to re-check onError because it might have changed during the
      // asynchronous gap.
      if (onError != null) onError(error);
    });
  }

  @override
  String toString() => 'ChannelReader($_channel)';

  void _handleWaitComplete(int status, int pending) {
    assert(isBound);
    if (status != ZX.OK) {
      close();
      _errorSoon(new ChannelReaderError(
          'Wait completed with status ${getStringForStatus(status)} ($status)',
          null));
      return;
    }
    // TODO(abarth): Change this try/catch pattern now that we don't use
    // RawReceivePort any more.
    try {
      if ((pending & Channel.READABLE) != 0) {
        if (onReadable != null) onReadable();
        if (isBound) _asyncWait();
      } else if ((pending & Channel.PEER_CLOSED) != 0) {
        close();
        _errorSoon(null);
      }
    } on Error catch (_) {
      // An Error exception from the core libraries is probably a programming
      // error that can't be handled. We rethrow the error so that
      // FidlEventHandlers can't swallow it by mistake.
      rethrow;
    } catch (e, s) {
      close();
      _errorSoon(new ChannelReaderError(e, s));
    }
  }
}
