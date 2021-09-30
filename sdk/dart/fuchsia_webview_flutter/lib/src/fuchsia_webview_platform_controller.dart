// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: avoid_as, import_of_legacy_library_into_null_safe

import 'dart:async';
import 'dart:convert' show utf8;
import 'dart:typed_data';

import 'package:fidl/fidl.dart' as fidl;
import 'package:fidl_fuchsia_net_http/fidl_async.dart' as fidl_net;
import 'package:fidl_fuchsia_web/fidl_async.dart' as fidl_web;
import 'package:fuchsia_logger/logger.dart';
import 'package:webview_flutter/platform_interface.dart';

import 'fuchsia_web_services.dart';
import 'utils.dart' as utils;

class _ChannelSubscription {
  StreamSubscription<String>? subscription;
  final int id;
  _ChannelSubscription(this.id, {this.subscription});
}

/// Fuchsia [WebViewPlatformController] implementation that serves as the entry
/// point for all [fuchsia_webview_flutter/webview.dart]'s apis
class FuchsiaWebViewPlatformController extends WebViewPlatformController {
  /// Helper class to interact with fuchsia web services
  FuchsiaWebServices? _fuchsiaWebServices;
  int _nextId = 0;
  var _currentState = fidl_web.NavigationState();
  // Reason: sdk_version_set_literal unsupported until version 2.2
  // ignore: prefer_collection_literals
  var _beforeLoadChannels = Set<String>();

  final WebViewPlatformCallbacksHandler _platformCallbacksHandler;
  final JavascriptChannelRegistry _javascriptChannelRegistry;
  final _javascriptChannelSubscriptions = <String, _ChannelSubscription>{};

  /// Initializes [FuchsiaWebViewPlatformController]
  FuchsiaWebViewPlatformController(
    CreationParams creationParams,
    this._platformCallbacksHandler,
    this._javascriptChannelRegistry,
    this._fuchsiaWebServices,
  ) : super(_platformCallbacksHandler) {
    fuchsiaWebServices.setNavigationEventListener(
        _WebviewNavigationEventListener(_onNavigationStateChanged));
    if (creationParams.webSettings != null) {
      updateSettings(creationParams.webSettings!);
    }
    _addBeforeLoadChannels(creationParams.javascriptChannelNames);
    if (creationParams.initialUrl != null) {
      loadUrl(creationParams.initialUrl!, {});
    }
  }

  /// Returns [FuchsiaWebServices]
  FuchsiaWebServices get fuchsiaWebServices {
    return _fuchsiaWebServices ??= FuchsiaWebServices();
  }

  @override
  Future<void> addJavascriptChannels(Set<String> javascriptChannelNames) async {
    await _createChannelSubscriptions(javascriptChannelNames);
  }

  @override
  Future<bool> canGoBack() async {
    if (_currentState.canGoBack != null) {
      return _currentState.canGoBack!;
    }
    return false;
  }

  @override
  Future<bool> canGoForward() async {
    if (_currentState.canGoBack != null) {
      return _currentState.canGoForward!;
    }
    return false;
  }

  @override
  Future<void> clearCache() {
    throw UnimplementedError(
        'FuchsiaWebView clearCache is not implemented on the current platform');
  }

  @override
  Future<String?> currentUrl() async {
    return _currentState.url;
  }

  @override
  Future<String?> getTitle() async {
    return _currentState.title;
  }

  @override
  Future<String> evaluateJavascript(String javascriptString) async {
    return fuchsiaWebServices.evaluateJavascript(['*'], javascriptString);
  }

  @override
  Future<void> goBack() {
    return fuchsiaWebServices.navigationController.goBack();
  }

  @override
  Future<void> goForward() {
    return fuchsiaWebServices.navigationController.goForward();
  }

  @override
  Future<void> loadUrl(
    String url,
    Map<String, String>? headers,
  ) async {
    final headersList = <fidl_net.Header>[];
    if (headers != null) {
      headers.forEach((k, v) {
        headersList.add(fidl_net.Header(
            name: utf8.encode(k) as Uint8List,
            value: utf8.encode(v) as Uint8List));
      });
    }

    return fuchsiaWebServices.navigationController.loadUrl(
        url,
        fidl_web.LoadUrlParams(
          type: fidl_web.LoadUrlReason.typed,
          headers: headersList,
        ));
  }

  @override
  Future<void> reload() {
    return fuchsiaWebServices.navigationController
        .reload(fidl_web.ReloadType.partialCache);
  }

  @override
  Future<void> removeJavascriptChannels(
      Set<String> javascriptChannelNames) async {
    for (final channelName in javascriptChannelNames) {
      if (_javascriptChannelSubscriptions.containsKey(channelName)) {
        await _javascriptChannelSubscriptions[channelName]!
            .subscription!
            .cancel();
        _javascriptChannelSubscriptions.remove(channelName);
        await fuchsiaWebServices
            .evaluateJavascript(['*'], 'window.$channelName = undefined;');
      }
    }
  }

  @override
  Future<void> updateSettings(WebSettings settings) {
    if (settings.debuggingEnabled != null) {
      return fuchsiaWebServices.setJavaScriptLogLevel(settings.debuggingEnabled!
          ? fidl_web.ConsoleLogLevel.debug
          : fidl_web.ConsoleLogLevel.none);
    }
    return Future.value();
  }

  /// Clears all cookies for all [WebView] instances.
  ///
  /// Returns true if cookies were present before clearing, else false.
  static Future<bool> clearCookies() {
    throw UnimplementedError(
        'FuchsiaWebView clearCookies is not implemented on the current platform');
  }

  /// Close all remaining subscriptions and connections.
  void dispose() {
    for (final entry in _javascriptChannelSubscriptions.entries) {
      entry.value.subscription!.cancel();
    }
    fuchsiaWebServices.dispose();
  }

  /// Called when a navigation state event is received from the webview.
  Future<void> _onNavigationStateChanged(fidl_web.NavigationState state) async {
    _updateCurrentStateDiff(state);
    if (state.isMainDocumentLoaded != null && state.isMainDocumentLoaded!) {
      await _establishCommunication(_beforeLoadChannels);
      _platformCallbacksHandler.onPageFinished(_currentState.url!);
    }
  }

  /// Updates the current state with each field that is set in the new
  /// navigation state.
  void _updateCurrentStateDiff(fidl_web.NavigationState state) {
    _currentState = fidl_web.NavigationState(
      title: state.title ?? _currentState.title,
      url: state.url ?? _currentState.url,
      canGoBack: state.canGoBack ?? _currentState.canGoBack,
      canGoForward: state.canGoForward ?? _currentState.canGoForward,
      isMainDocumentLoaded:
          state.isMainDocumentLoaded ?? _currentState.isMainDocumentLoaded,
      pageType: state.pageType ?? _currentState.pageType,
    );
  }

  /// Registers the javascript channels that will be loaded when the page loads.
  /// Connection will be established when the page loads.
  Future<void> _addBeforeLoadChannels(
      Set<String> javascriptChannelNames) async {
    _beforeLoadChannels = javascriptChannelNames;
    await _createChannelSubscriptions(javascriptChannelNames, beforeLoad: true);
  }

  /// For each channel in [javascriptChannelNames] creates an object with the
  /// channel name on window in the frame. That object will contain a
  /// `postMessage` method. Messages sent through that method will be received
  /// here and notified back to the client of the webview.
  /// The process for each channel is:
  ///   1. Inject the script that will create the object on window to the
  ///      webview. This script will initially wait for a 'share-port' message.
  ///   2. postMessage 'share-port' to the frame.
  ///   3. The frame has a listener on window and will reply with a
  ///      share-port-ack and a port to which the frame will send messages.
  ///   4. Bind that port and start listening on it.
  ///   5. Notify the frame that we are ready to receive messages.
  ///   6. When a message arrives on that port it is sent to the client through
  ///      the platform callback.
  Future<void> _createChannelSubscriptions(Set<String> javascriptChannelNames,
      {bool beforeLoad = false}) async {
    for (final channelName in javascriptChannelNames) {
      // Close any connections to that object (if any existed)
      if (_javascriptChannelSubscriptions.containsKey(channelName)) {
        await _javascriptChannelSubscriptions[channelName]!
            .subscription!
            .cancel();
      } else {
        _javascriptChannelSubscriptions[channelName] =
            _ChannelSubscription(_nextId);
        _nextId += 1;
      }
    }

    // Create a JavaScript object with one postMessage method. This object
    // will be exposed on window.$channelName when the FIDL communication is
    // established. Any window.$channelName already set will be removed.
    // If before load, the connection will happen once the page loads.
    if (beforeLoad) {
      await Future.wait(javascriptChannelNames.map((channel) {
        // Reason: not supported until v2.2 and we need to support earlier
        // versions.
        // ignore: prefer_collection_literals
        final script = _scriptForChannels(Set.from([channel]));
        return fuchsiaWebServices.evaluateJavascriptBeforeLoad(
            _javascriptChannelSubscriptions[channel]!.id, ['*'], script);
      }));
      return;
    }

    final script = _scriptForChannels(javascriptChannelNames);
    await evaluateJavascript(script);

    await _establishCommunication(javascriptChannelNames);
  }

  Future<void> _establishCommunication(Set<String> javascriptChannelNames) {
    return Future.wait(javascriptChannelNames.map((channelName) async {
      // Creates the message channel connection.
      fidl_web.MessagePortProxy incomingPort;
      try {
        incomingPort = await _bindIncomingPort(channelName);
      } on Exception catch (e) {
        log.warning('Failed to bind incoming port for $channelName: $e');
        return;
      }

      // Subscribe for incoming messages.
      final incomingMessagesStream = _startReceivingMessages(incomingPort);
      _javascriptChannelSubscriptions[channelName]!.subscription =
          incomingMessagesStream.listen(
        (message) async {
          _javascriptChannelRegistry.onJavascriptChannelMessage(
              channelName, message);
        },
      );

      // Notify of readiness
      await fuchsiaWebServices.postMessage(
          '*', 'share-port-$channelName-ready');
    }));
  }

  /// Communicates with the script injected by `_scriptForChannels` to get a port
  /// from the web page with which to communicate with the page. See comments on
  /// `_createChannelSubscriptions` for details on the process.
  Future<fidl_web.MessagePortProxy> _bindIncomingPort(String channel) async {
    final messagePort = fidl_web.MessagePortProxy();
    await fuchsiaWebServices.postMessage('*', 'share-port-$channel',
        outgoingMessagePortRequest: messagePort.ctrl.request());

    final msg = await messagePort.receiveMessage();
    final ackMsg = utils.bufferToString(msg.data!);
    if (ackMsg != 'share-port-ack-$channel') {
      throw Exception('Expected "share-port-ack-$channel", got: "$ackMsg"');
    }
    if (msg.incomingTransfer == null || msg.incomingTransfer!.isEmpty) {
      throw Exception('failed to provide an incoming message port');
    }
    final incomingMessagePort = fidl_web.MessagePortProxy();
    incomingMessagePort.ctrl.bind(msg.incomingTransfer![0].messagePort!);
    return incomingMessagePort;
  }

  /// Script injected to the frame to create an object with the given name on
  /// window. See comments on `_createChannelSubscriptions` for details on the
  /// process.
  String _scriptForChannels(Set<String> channelNames) {
    return channelNames.map((channel) => '''
        (function() {
          class $channel {
            constructor() {
              this._messageChannel = null;
              this._pendingMessages = [];
              this._isReady = false;
            }

            postMessage(message) {
              if (this._isReady) {
                this._messageChannel.port1.postMessage(message);
              } else {
                this._pendingMessages.push(message);
              }
            }

            _ready() {
              for (const pendingMessage of this._pendingMessages) {
                this._messageChannel.port1.postMessage(pendingMessage);
              }
              this._isReady = true;
              this._pendingMessages = [];
            }

            _setMessageChannel(channel) {
              this._messageChannel = channel;
            }
          }

          window.$channel = new $channel();

          function initializer$channel(event) {
            if (event.data) {
              if (event.data == 'share-port-$channel' && event.ports && event.ports.length > 0) {
                console.log('Registering channel $channel');
                const messageChannel = new MessageChannel();
                window.$channel._setMessageChannel(messageChannel);
                event.ports[0].postMessage('share-port-ack-$channel', [messageChannel.port2]);
              }
              if (event.data == 'share-port-$channel-ready') {
                console.log('Channel $channel ready');
                window.$channel._ready();
                window.removeEventListener('message', initializer$channel, true);
              }
            }
          }
          window.addEventListener('message', initializer$channel, true);
        })();
      ''').join('\n');
  }

  /// Listens for messages on the incoming port and streams them.
  Stream<String> _startReceivingMessages(
      fidl_web.MessagePortProxy incomingMessagePort) async* {
    // ignore: literal_only_boolean_expressions
    while (true) {
      try {
        final msg = await incomingMessagePort.receiveMessage();
        yield utils.bufferToString(msg.data!);
      } on fidl.FidlError {
        // Occurs when the incoming port is closed (ie navigate to another page).
        break;
      }
    }
  }
}

class _WebviewNavigationEventListener extends fidl_web.NavigationEventListener {
  final Future<void> Function(fidl_web.NavigationState state)
      navigationStateCallback;

  _WebviewNavigationEventListener(this.navigationStateCallback);

  @override
  Future<void> onNavigationStateChanged(fidl_web.NavigationState state) async {
    await navigationStateCallback(state);
  }
}
