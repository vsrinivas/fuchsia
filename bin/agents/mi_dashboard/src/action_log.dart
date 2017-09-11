// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io';

import 'package:lib.app.dart/app.dart';
import 'package:apps.maxwell.services.action_log/listener.fidl.dart';
import 'package:apps.maxwell.services.action_log/user.fidl.dart';

import 'data_handler.dart';

class ActionLogDataHandler extends ActionLogListener with DataHandler {
  @override
  String get name => "action_log";

  // cache for current context state
  final _actionLogCache = new List<UserAction>();

  // connection to ActionLog
  UserActionLogProxy _actionLog;
  ActionLogListenerBinding _actionLogListener;

  SendWebSocketMessage _sendMessage;

  @override
  void init(ApplicationContext appContext, SendWebSocketMessage sender) {
    this._sendMessage = sender;

    // Connect to the ActionLog
    _actionLog = new UserActionLogProxy();
    _actionLogListener = new ActionLogListenerBinding();
    connectToService(appContext.environmentServices, _actionLog.ctrl);
    assert(_actionLog.ctrl.isBound);

    // Subscribe to ActionLog Actions
    _actionLog.subscribe(_actionLogListener.wrap(this));
  }

  @override
  bool handleRequest(String requestString, HttpRequest request) {
    // /data/action_log/all returns all of the actions in the log
    if (requestString == "/all") {
      request.response.write(JSON.encode(_actionLogCache));
      request.response.close();
      return true;
    }
    return false;
  }

  @override
  void handleNewWebSocket(WebSocket socket) {
    // send all cached data to the socket
    String message = JSON.encode({"action_log.all": _actionLogCache});
    socket.add(message);
  }

  @override
  void onAction(UserAction action) {
    _actionLogCache.add(action);
    this._sendMessage(JSON.encode({"action_log.new_action": action.toJson()}));
  }
}
