// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io';

import 'package:application.lib.app.dart/app.dart';
import 'package:apps.maxwell.lib.context.dart/context_listener_impl.dart';
import 'package:apps.maxwell.services.context/context_provider.fidl.dart';

import 'data_handler.dart';

class ContextDataHandler extends DataHandler {
  @override
  String get name => "context";

  // cache for current context state
  final _contextCache = new Map<String, String>();

  // connection to context provider
  ContextProviderProxy _contextProvider;
  ContextListenerImpl _contextListener;

  SendWebSocketMessage _sendMessage;

  @override
  void init(ApplicationContext appContext, SendWebSocketMessage sender) {
    this._sendMessage = sender;

    // Connect to the ContextProvider
    _contextProvider = new ContextProviderProxy();
    _contextListener = new ContextListenerImpl(this.onContextUpdate);
    connectToService(appContext.environmentServices, _contextProvider.ctrl);
    assert(_contextProvider.ctrl.isBound);

    // Subscribe to all topics
    ContextQuery query = new ContextQuery();
    query.topics = []; // empty list is the wildcard query
    _contextProvider.subscribe(query, _contextListener.getHandle());
  }

  @override
  bool handleRequest(String requestString, HttpRequest request) {
    // The requestString will contain just the topic
    // /data/context/<topic>
    //     return JSON data from the context service for the given topic
    var topic = requestString;
    var topicValue = _contextCache[topic];
    // print("[DASHBOARD] Request for context topic ${topic} with value ${topicValue}");
    if (topicValue != null) {
      // Write the data to the response.
      request.response.write(topicValue);
      request.response.close();
      return true;
    }
    return false;
  }

  @override
  void handleNewWebSocket(WebSocket socket) {
    // send all cached context data to the socket
    String message = JSON.encode({
      "context.update": _contextCache
    });
    socket.add(message);
  }

  void onContextUpdate(ContextUpdate update) {
    // Cache all context values that we receive
    update.values.forEach((String key, String value) {
      // print("[DASHBOARD UPDATE] ${key}: ${value}");
      _contextCache[key] = value;
    });

    this._sendMessage(JSON.encode({"context.update": update.values}));
  }
}
