// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of core;

class ChannelReaderError {
  final Object error;
  final StackTrace stacktrace;

  ChannelReaderError(this.error, this.stacktrace);

  @override
  String toString() => error.toString();
}

typedef void ChannelReaderErrorHandler(ChannelReaderError error);

class ChannelReader {
  Channel get channel => _channel;
  Channel _channel;

  bool get isBound => _channel != null;

  HandleWaiter _waiter;

  VoidCallback onReadable;
  ChannelReaderErrorHandler onError;

  void bind(Channel channel) {
    if (isBound)
      throw new FidlApiError('ChannelReader is already bound.');
    _channel = channel;
    _waiter ??= new HandleWaiter();
    _asyncWait();
  }

  Channel unbind() {
    if (!isBound)
      throw new FidlApiError("ChannelReader is not bound");
    _waiter.cancelWait();
    final Channel result = _channel;
    _channel = null;
    return result;
  }

  void close() {
    if (!isBound)
      return;
    _waiter.cancelWait();
    _channel.close();
    _channel = null;
  }

  void _asyncWait() {
    _waiter.asyncWait(
      channel.handle,
      MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
      MX_TIME_INFINITE,
      _handleWaitComplete
    );
  }

  void _errorSoon(ChannelReaderError error) {
    if (onError == null)
      return;
    scheduleMicrotask(() {
      // We need to re-check onError because it might have changed during the
      // asynchronous gap.
      if (onError != null)
        onError(error);
    });
  }

  @override
  String toString() => 'ChannelReader($_channel)';

  void _handleWaitComplete(int status, int pending) {
    assert(isBound);
    if (status != NO_ERROR) {
      close();
      _errorSoon(new ChannelReaderError(
          'Wait completed with status ${getStringForStatus(status)} ($status)',
          null));
      return;
    }
    // This callback is running in the handler for a RawReceivePort. All
    // exceptions rethrown or not caught here will be unhandled exceptions in
    // the root zone, bringing down the whole app. An app should rather have an
    // opportunity to handle exceptions coming from message handles, like the
    // FidlCodecError.
    // TODO(zra): Rather than hard-coding a list of exceptions that bypass the
    // onError callback and are rethrown, investigate allowing an implementer to
    // provide a filter function (possibly initialized with a sensible default).
    try {
      if ((pending & MX_SIGNAL_READABLE) != 0) {
        if (onReadable != null)
          onReadable();
        if (isBound)
          _asyncWait();
      } else if ((pending & MX_SIGNAL_PEER_CLOSED) != 0) {
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
