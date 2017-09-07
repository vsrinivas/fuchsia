// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of bindings;

typedef void _VoidCallback();

/// A channel over which messages from interface T can be sent.
///
/// An interface handle holds a [channel] whose peer expects to receive messages
/// from the FIDL interface T. The channel held by an interface handle is not
/// currently bound, which means messages cannot yet be exchanged with the
/// channel's peer.
///
/// To send messages over the channel, bind the interface handle to a `TProxy`
/// object using use [ProxyController<T>.bind] method on the proxy's
/// [Proxy<T>.ctrl] property.
///
/// Example:
///
/// ```dart
/// InterfaceHandle<T> fooHandle = [...]
/// FooProxy foo = new FooProxy();
/// foo.ctrl.bind(fooHandle);
/// foo.bar();
/// ```
///
/// To obtain an interface handle to send over a channel, used the
/// [Binding<T>.wrap] method on an object of type `TBinding`.
///
/// Example:
///
/// ```dart
/// class FooImpl extends Foo {
///   final FooBinding _binding = new FooBinding();
///
///   InterfaceHandle<T> getInterfaceHandle() => _binding.wrap(this);
///
///   @override
///   void bar() {
///     print('Received bar message.');
///   }
/// }
/// ```
class InterfaceHandle<T> {
  /// Creates an interface handle that wraps the given channel.
  InterfaceHandle(this._channel, this.version);

  /// The underlying channel messages will be sent over when the interface
  /// handle is bound to a [Proxy].
  ///
  /// To take the channel from this object, use [passChannel].
  core.Channel get channel => _channel;
  core.Channel _channel;

  /// The version of the interface this object expects at the remote end of the
  /// channel.
  final int version;

  /// Returns [channel] and sets [channel] to null.
  ///
  /// Useful for taking ownership of the underlying channel.
  core.Channel passChannel() {
    final core.Channel result = _channel;
    _channel = null;
    return result;
  }

  /// Closes the underlying channel.
  void close() {
    _channel?.close();
    _channel = null;
  }
}

/// A channel over which messages from interface T can be received.
///
/// An interface request holds a [channel] whose peer expects to be able to send
/// messages from the FIDL interface T. A channel held by an interface request
/// is not currently bound, which means messages cannot yet be exchanged with
/// the channel's peer.
///
/// To receive messages sent over the channel, bind the interface handle using
/// [Binding<T>.bind] on a `TBinding` object, which you typically hold as a
/// private member variable in a class that implements [T].
///
/// Example:
///
/// ```dart
/// class FooImpl extends Foo {
///   final FooBinding _binding = new FooBinding();
///
///   void bind(InterfaceRequest<T> request) {
///     _binding.bind(request);
///   }
///
///   @override
///   void bar() {
///     print('Received bar message.');
///   }
/// }
/// ```
///
/// To obtain an interface request to send over a channel, used the
/// [ProxyController<T>.request] method on the [Proxy<T>.ctrl] property of an
/// object of type `TProxy`.
///
/// Example:
///
/// ```dart
/// FooProxy foo = new FooProxy();
/// InterfaceRequest<T> request = foo.ctrl.request();
/// ```
class InterfaceRequest<T> {
  /// Creates an interface request that wraps the given channel.
  InterfaceRequest(this._channel);

  /// The underlying channel messages will be received over when the interface
  /// handle is bound to [Binding].
  ///
  /// To take the channel from this object, use [passChannel].
  core.Channel get channel => _channel;
  core.Channel _channel;

  /// Returns [channel] and sets [channel] to null.
  ///
  /// Useful for taking ownership of the underlying channel.
  core.Channel passChannel() {
    final core.Channel result = _channel;
    _channel = null;
    return result;
  }

  /// Closes the underlying channel.
  void close() {
    _channel?.close();
    _channel = null;
  }
}

class InterfacePair<T> {
  InterfacePair() {
    core.ChannelPair pair = new core.ChannelPair();
    request = new InterfaceRequest<T>(pair.channel0);
    handle = new InterfaceHandle<T>(pair.channel1, 0);
  }

  InterfaceRequest<T> request;
  InterfaceHandle<T> handle;

  InterfaceRequest<T> passRequest() {
    final InterfaceRequest<T> result = request;
    request = null;
    return result;
  }

  InterfaceHandle<T> passHandle() {
    final InterfaceHandle<T> result = handle;
    handle = null;
    return result;
  }
}

/// Listens for messages and dispatches them to an implementation of T.
abstract class Binding<T> {
  /// Creates a binding object in an unbound state.
  ///
  /// Rather than creating a [Binding<T>] object directly, you typically create
  /// a `TBinding` object, which are subclasses of [Binding<T>] created by the
  /// FIDL compiler for a specific interface.
  Binding() {
    _reader.onReadable = _handleReadable;
    _reader.onError = _handleError;
  }

  /// Returns an interface handle whose peer is bound to the given object.
  ///
  /// Creates a channel pair, binds one of the channels to this object, and
  /// returns the other channel. Messages sent over the returned channel will be
  /// decoded and dispatched to `impl`.
  ///
  /// The `impl` parameter must not be null.
  InterfaceHandle<T> wrap(T impl) {
    assert(!isBound);
    core.ChannelPair pair = new core.ChannelPair();
    if (pair.status != ZX.OK) return null;
    _impl = impl;
    _reader.bind(pair.passChannel0());
    return new InterfaceHandle<T>(pair.passChannel1(), version);
  }

  /// Binds the given implementation to the given interface request.
  ///
  /// Listens for messages on channel underlying the given interface request,
  /// decodes them, and dispatches the decoded messages to `impl`.
  ///
  /// This object must not already be bound.
  ///
  /// The `impl` and `interfaceRequest` parameters must not be null. The
  /// `channel` property of the given `interfaceRequest` must not be null.
  void bind(T impl, InterfaceRequest<T> interfaceRequest) {
    assert(!isBound);
    assert(impl != null);
    assert(interfaceRequest != null);
    core.Channel channel = interfaceRequest.passChannel();
    assert(channel != null);
    _impl = impl;
    _reader.bind(channel);
  }

  /// Unbinds [impl] and returns the unbound channel as an interface request.
  ///
  /// Stops listening for messages on the bound channel, wraps the channel in an
  /// interface request of the appropriate type, and returns that interface
  /// request.
  ///
  /// The object must have previously been bound (e.g., using [bind]).
  InterfaceRequest<T> unbind() {
    assert(isBound);
    final InterfaceRequest<T> result =
        new InterfaceRequest<T>(_reader.unbind());
    _impl = null;
    return result;
  }

  /// Close the bound channel.
  ///
  /// This function does nothing if the object is not bound.
  void close() {
    if (isBound) {
      _reader.close();
      _impl = null;
    }
  }

  /// Called when the channel underneath closes.
  _VoidCallback onConnectionError;

  /// The implementation of [T] bound using this object.
  ///
  /// If this object is not bound, this property is null.
  T get impl => _impl;
  T _impl;

  /// Whether this object is bound to a channel.
  ///
  /// See [bind] and [unbind] for more information.
  bool get isBound => _impl != null;

  /// Decodes the given message and dispatches the decoded message to [impl].
  ///
  /// This function is called by this object whenever a message arrives over a
  /// bound channel.
  @protected
  void handleMessage(ServiceMessage message, MessageSink respond);

  /// The version of [T] implemented by this object.
  int get version;

  void _handleReadable() {
    final core.ReadResult result = _reader.channel.queryAndRead();
    if ((result.bytes == null) || (result.bytes.lengthInBytes == 0))
      throw new FidlCodecError('Unexpected empty message or error: $result');

    final Message message = new Message.fromReadResult(result);
    handleMessage(new ServiceMessage.fromMessage(message), _sendResponse);
  }

  /// Always called when the channel underneath closes. If [onConnectionError]
  /// is set, it is called.
  void _handleError(core.ChannelReaderError error) {
    if (onConnectionError != null) onConnectionError();
  }

  void _sendResponse(Message response) {
    if (!_reader.isBound) return;
    final int status = _reader.channel.write(response.buffer, response.handles);
    // ZX.ERR_BAD_STATE is only used to indicate that the other end of
    // the pipe has been closed. We can ignore the close here and wait for
    // the PeerClosed signal on the event stream.
    assert((status == ZX.OK) || (status == ZX.ERR_BAD_STATE));
  }

  final core.ChannelReader _reader = new core.ChannelReader();
}

/// The object that [ProxyController<T>.error] completes with when there is
/// an error.
class ProxyError {
  /// Creates a proxy error with the given message.
  ///
  /// The `message` argument must not be null.
  ProxyError(this.message);

  /// What went wrong.
  final String message;

  @override
  String toString() => 'ProxyError: $message';
}

/// The control plane for an interface proxy.
///
/// A proxy controller lets you operate on the local [Proxy<T>] object itself
/// rather than send messages to the remote implementation of the proxy. For
/// example, you can [unbind] or [close] the proxy.
///
/// You typically obtain a [ProxyController<T>] object as the [Proxy<T>.ctrl]
/// property of a `TProxy` object.
///
/// Example:
///
/// ```dart
/// FooProxy foo = new FooProxy();
/// fooProvider.getFoo(foo.ctrl.request());
/// ```
class ProxyController<T> {
  /// Creates proxy controller.
  ///
  /// Proxy controllers are not typically created directly. Instead, you
  /// typically obtain a [ProxyController<T>] object as the [Proxy<T>.ctrl]
  /// property of a `TProxy` object.
  ProxyController({this.serviceName}) {
    _reader.onReadable = _handleReadable;
    _reader.onError = _handleError;
  }

  /// The service name associated with [T], if any.
  ///
  /// Corresponds to the `[ServiceName]` attribute in the FIDL interface
  /// definition.
  ///
  /// This string is typically used with the `ServiceProvider` interface to
  /// request an implementation of [T].
  final String serviceName;

  /// Creates an interface request whose peer is bound to this interface proxy.
  ///
  /// Creates a channel pair, binds one of the channels to this object, and
  /// returns the other channel. Calls to the proxy will be encoded as messages
  /// and sent to the returned channel.
  ///
  /// The proxy must not already have been bound.
  ///
  /// The `version` parameter must not be null.
  InterfaceRequest<T> request({int version: 0}) {
    assert(version != null);
    assert(!isBound);
    core.ChannelPair pair = new core.ChannelPair();
    assert(pair.status == ZX.OK);
    _version = version;
    _reader.bind(pair.passChannel0());
    return new InterfaceRequest<T>(pair.passChannel1());
  }

  /// Binds the proxy to the given interface handle.
  ///
  /// Calls to the proxy will be encoded as messages and sent over the channel
  /// underlying the given interface handle.
  ///
  /// This object must not already be bound.
  ///
  /// The `interfaceHandle` parameter must not be null. The `channel` property
  /// of the given `interfaceHandle` must not be null.
  void bind(InterfaceHandle<T> interfaceHandle) {
    assert(!isBound);
    assert(interfaceHandle != null);
    assert(interfaceHandle.channel != null);
    _version = interfaceHandle.version;
    _reader.bind(interfaceHandle.passChannel());
  }

  /// Unbinds the proxy and returns the unbound channel as an interface handle.
  ///
  /// Calls on the proxy will no longer be encoded as messages on the bound
  /// channel.
  ///
  /// The proxy must have previously been bound (e.g., using [bind]).
  InterfaceHandle<T> unbind() {
    assert(isBound);
    if (!_reader.isBound) return null;
    return new InterfaceHandle<T>(_reader.unbind(), _version);
  }

  /// Whether this object is bound to a channel.
  ///
  /// See [bind] and [unbind] for more information.
  bool get isBound => _reader.isBound;

  /// Close the channel bound to the proxy.
  ///
  /// The proxy must have previously been bound (e.g., using [bind]).
  void close() {
    if (isBound) {
      if (_pendingResponsesCount > 0) proxyError('The proxy is closed.');
      _reset();
      _reader.close();
    }
  }

  /// Called when the channel underneath closes.
  _VoidCallback onConnectionError;

  /// Called whenever this object receives a response on a bound channel.
  ///
  /// Used by subclasses of [Proxy<T>] to receive responses to messages.
  MessageSink onResponse;

  final core.ChannelReader _reader = new core.ChannelReader();
  final HashMap<int, Function> _callbackMap = new HashMap<int, Function>();

  /// A future that completes when an error is generated by the proxy.
  Future<ProxyError> get error => _errorCompleter.future;
  Completer<ProxyError> _errorCompleter = new Completer<ProxyError>();

  /// Version of this interface that the remote side supports.
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
    final core.ReadResult result = _reader.channel.queryAndRead();
    if ((result.bytes == null) || (result.bytes.lengthInBytes == 0)) {
      proxyError('Read from channel failed');
      return;
    }
    try {
      _pendingResponsesCount--;
      if (onResponse != null) {
        final Message message = new Message.fromReadResult(result);
        onResponse(new ServiceMessage.fromMessage(message));
      }
    } on FidlCodecError catch (e) {
      if (result.handles != null)
        result.handles.forEach((handle) => handle.close());
      proxyError(e.toString());
      close();
    }
  }

  /// Always called when the channel underneath closes. If [onConnectionError]
  /// is set, it is called.
  void _handleError(core.ChannelReaderError error) {
    if (onConnectionError != null) onConnectionError();
  }

  /// Sends the given messages over the bound channel.
  ///
  /// Used by subclasses of [Proxy<T>] to send encoded messages.
  void sendMessage(Struct message, int name) {
    if (!_reader.isBound) {
      proxyError('The proxy is closed.');
      return;
    }
    final ServiceMessage serialized =
        message.serializeWithHeader(new MessageHeader(name));
    final int status =
        _reader.channel.write(serialized.buffer, serialized.handles);
    if (status != ZX.OK)
      proxyError(
          'Failed to write to channel: ${_reader.channel} (status: $status)');
  }

  /// Sends the given messages over the bound channel and registers a callback
  /// to handle the response.
  ///
  /// Used by subclasses of [Proxy<T>] to send encoded messages.
  void sendMessageWithRequestId(
      Struct message, int name, int id, int flags, Function callback) {
    if (!_reader.isBound) {
      proxyError('The sender is closed.');
      return;
    }

    final int messageId = (id == -1) ? _nextId++ : id;

    final MessageHeader header =
        new MessageHeader.withRequestId(name, flags, messageId);
    final ServiceMessage serialized = message.serializeWithHeader(header);

    final int status =
        _reader.channel.write(serialized.buffer, serialized.handles);

    if (status != ZX.OK) {
      proxyError(
          'Failed to write to channel: ${_reader.channel} (status: $status)');
      return;
    }

    _callbackMap[messageId] = callback;
    _pendingResponsesCount++;
  }

  /// Returns the callback associated with the given response message.
  ///
  /// Used by subclasses of [Proxy<T>] to retrieve registered callbacks when
  /// handling response messages.
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

  /// Complete the [error] future with the given message.
  void proxyError(String message) {
    if (!_errorCompleter.isCompleted) {
      error.whenComplete(() {
        _errorCompleter = new Completer();
      });
      _errorCompleter.complete(new ProxyError(message));
    }
  }
}

/// Sends messages to a remote implementation of [T]
class Proxy<T> {
  /// Creates a proxy object with the given [ctrl].
  ///
  /// Rather than creating [Proxy<T>] object directly, you typically create
  /// `TProxy` objects, which are subclasses of [Proxy<T>] created by the FIDL
  /// compiler for a specific interface.
  Proxy(this.ctrl);

  /// The control plane for this proxy.
  ///
  /// Methods that manipulate the local proxy (as opposed to sending messages
  /// to the remote implementation of [T]) are exposed on this [ctrl] object to
  /// avoid naming conflicts with the methods of [T].
  final ProxyController<T> ctrl;

  // In general it's probably better to avoid adding fields and methods to this
  // class. Names added to this class have to be mangled by bindings generation
  // to avoid name conflicts.
}
