// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of bindings;

class InterfaceHandle<T> {
  InterfaceHandle(this._channel, this.version);

  core.Channel get channel => _channel;
  core.Channel _channel;

  final int version;

  core.Channel passChannel() {
    final core.Channel result = _channel;
    _channel = null;
    return result;
  }
}

class InterfaceRequest<T> {
  InterfaceRequest(this._channel);

  core.Channel get channel => _channel;
  core.Channel _channel;

  core.Channel passChannel() {
    final core.Channel result = _channel;
    _channel = null;
    return result;
  }
}

abstract class Binding<T> {
  Binding() {
    _reader.onReadable = _handleReadable;
  }

  InterfaceHandle<T> wrap(T impl) {
    core.ChannelPair pair = new core.ChannelPair();
    if (pair.status != core.NO_ERROR)
      return null;
    _impl = impl;
    _reader.bind(pair.passChannel0());
    return new InterfaceHandle<T>(pair.passChannel1(), version);
  }

  void bind(T impl, InterfaceRequest<T> interfaceRequest) {
    _impl = impl;
    _reader.bind(interfaceRequest.passChannel());
  }

  InterfaceRequest<T> unbind() {
    final InterfaceRequest<T> result =
        new InterfaceRequest<T>(_reader.unbind());
    _impl = null;
    return result;
  }

  T get impl => _impl;
  T _impl;

  void handleMessage(ServiceMessage message, MessageSink respond);
  int get version;

  void _handleReadable() {
    final result = _reader.channel.queryAndRead();
    if ((result.data == null) || (result.dataLength == 0))
      throw new FidlCodecError('Unexpected empty message or error: $result');

    try {
      final Message message = new Message(result.data,
                                          result.handles,
                                          result.dataLength,
                                          result.handlesLength);
      handleMessage(new ServiceMessage.fromMessage(message), _sendResponse);
    } catch (e) {
      // TODO(abarth): This exception handler might be doing more harm than
      // good. Some of the handles might have ended up in useful places.
      if (result.handles != null)
        result.handles.forEach((handle) => handle.close());
      rethrow;
    }
  }

  void _sendResponse(Message response) {
    if (!_reader.isBound)
      return;
    final int status = _reader.channel.write(response.buffer,
                                             response.buffer.lengthInBytes,
                                             response.handles);
    // ERR_BAD_STATE is only used to indicate that the other end of
    // the pipe has been closed. We can ignore the close here and wait for
    // the PeerClosed signal on the event stream.
    assert((status == core.NO_ERROR) || (status == core.ERR_BAD_STATE));
  }

  final core.ChannelReader _reader = new core.ChannelReader();
}

/// The object that [ProxyController.error] completes with when there is
/// an error.
class ProxyError {
  ProxyError(this.message);

  final String message;

  String toString() => 'ProxyError: $message';
}

class ProxyController<T> {
  ProxyController({ this.serviceName }) {
    _reader.onReadable = _handleReadable;
  }

  final String serviceName;

  InterfaceRequest<T> request({ int version: 0 }) {
    core.ChannelPair pair = new core.ChannelPair();
    if (pair.status != core.NO_ERROR)
      return null;
    _version = version;
    _reader.bind(pair.passChannel0());
    return new InterfaceRequest<T>(pair.passChannel1());
  }

  void bind(InterfaceHandle<T> interfaceHandle) {
    _version = interfaceHandle.version;
    _reader.bind(interfaceHandle.passChannel());
  }

  InterfaceHandle<T> unbind() {
    if (!_reader.isBound)
      return null;
    return new InterfaceHandle<T>(_reader.unbind(), _version);
  }

  bool get isBound => _reader.isBound;

  void close() {
    if (_pendingResponsesCount > 0)
      proxyError('The proxy is closed.');
    _reset();
    _reader.close();
  }

  MessageSink onResponse;

  final core.ChannelReader _reader = new core.ChannelReader();
  final HashMap<int, Function> _callbackMap = new HashMap<int, Function>();

  /// If there is an error in using this proxy, this future completes with
  /// a ProxyError.
  Future get error => _errorCompleter.future;
  Completer<ProxyError> _errorCompleter = new Completer<ProxyError>();

  /// Version of this interface that the remote side supports. Updated when a
  /// call to [queryVersion] or [requireVersion] is made.
  int get version => _version;
  int _version = 0;

  int _nextId = 0;
  int _pendingResponsesCount = 0;

  void _reset() {
    _callbackMap.clear();
    _errorCompleter = new Completer<ProxyError>();
    _version = 0;
    _nextId = 0;
    _pendingResponsesCount = 0;
  }

  void _handleReadable() {
    final result = _reader.channel.queryAndRead();
    if ((result.data == null) || (result.dataLength == 0)) {
      proxyError('Read from channel failed');
      return;
    }
    try {
      _pendingResponsesCount--;
      if (onResponse != null) {
        final Message message = new Message(result.data,
                                            result.handles,
                                            result.dataLength,
                                            result.handlesLength);
        onResponse(new ServiceMessage.fromMessage(message));
      }
    } on FidlCodecError catch (e) {
      if (result.handles != null)
        result.handles.forEach((handle) => handle.close());
      proxyError(e.toString());
      close();
    }
  }

  void sendMessage(Struct message, int name) {
    if (!_reader.isBound) {
      proxyError('The proxy is closed.');
      return;
    }
    final ServiceMessage serialized =
        message.serializeWithHeader(new MessageHeader(name));
    final int status = _reader.channel.write(serialized.buffer,
                                             serialized.buffer.lengthInBytes,
                                             serialized.handles);
    if (status != core.NO_ERROR)
      proxyError('Failed to write to channel: ${_reader.channel} (status: $status)');
  }

  void sendMessageWithRequestId(Struct message,
                                int name,
                                int id,
                                int flags,
                                Function callback) {
    if (!_reader.isBound) {
      proxyError('The sender is closed.');
      return;
    }

    if (id == -1) {
      id = _nextId++;
    }

    final MessageHeader header =
        new MessageHeader.withRequestId(name, flags, id);
    final ServiceMessage serialized =
        message.serializeWithHeader(header);

    final int status = _reader.channel.write(serialized.buffer,
                                             serialized.buffer.lengthInBytes,
                                             serialized.handles);

    if (status != core.NO_ERROR) {
      proxyError('Failed to write to channel: ${_reader.channel} (status: $status)');
      return;
    }

    _callbackMap[id] = callback;
    _pendingResponsesCount++;
  }

  Function getCallback(ServiceMessage message) {
    if (!message.header.hasRequestId) {
      proxyError('Expected a message with a valid request id.');
      return null;
    }
    final int requestId = message.header.requestId;
    final Function result = _callbackMap.remove(requestId);
    if (result == null) {
      proxyError('Message had unknown request id: $requestId');
      return null;
    }
    return result;
  }

  void proxyError(String message) {
    if (!_errorCompleter.isCompleted) {
      error.whenComplete(() {
        _errorCompleter = new Completer();
      });
      _errorCompleter.complete(new ProxyError(message));
    }
  }
}

class Proxy<T> {
  Proxy(this.ctrl);

  final ProxyController<T> ctrl;

  // In general it's probably better to avoid adding fields and methods to this
  // class. Names added to this class have to be mangled by bindings generation
  // to avoid name conflicts.
}
