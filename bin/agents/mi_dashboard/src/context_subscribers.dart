// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io';

import 'package:application.lib.app.dart/app.dart';
import 'package:apps.maxwell.services.context/context_reader.fidl.dart';

// TODO(rosswang): A note on the badness - Right now the generated package name
// for the context:debug target is context..debug, and likewise user:scope is
// user..scope. However, debug.fidl.dart imports user/scope.fidl.dart rather
// than user..scope/scope.fidl.dart, which are "incompatible". However, fear
// not -- user/scope.fidl.dart is available by depending on user:user_dart
// rather than user:scope_dart.
//
// The Atom Dart analyzer is displeased in all cases.
//
// Per e-mail thread, the fix for this will probably come after FIDL 2. In the
// meantime a less hacky workaround may be to just not use subtargets for FIDL.
import 'package:apps.maxwell.services.context..debug/debug.fidl.dart';
import 'package:apps.maxwell.services.user/scope.fidl.dart';

import 'data_handler.dart';

class ContextSubscribersDataHandler extends SubscriberListener
    with DataHandler {
  @override
  String get name => "context_subscribers";

  // Some Dart FIDL bindings do have toJson methods, but not unions. Since we need
  // to do some converting anyway, might as well also unwrap the more nested FIDL
  // bindings.
  // ignore: non_constant_identifier_names
  final JsonCodec JSON = new JsonCodec(toEncodable: (dynamic object) {
    if (object is ComponentScope) {
      switch (object.tag) {
        case ComponentScopeTag.globalScope:
          return {"type": "global"};
        case ComponentScopeTag.moduleScope:
          return {
            "type": "module",
            "url": object.moduleScope.url,
            "storyId": object.moduleScope.storyId
          };
        case ComponentScopeTag.agentScope:
          return {"type": "agent", "url": object.agentScope.url};
        default:
          return {"type": "unknown"};
      }
    } else if (object is ContextQuery) {
      return object.topics;
    } else {
      return object.toJson();
    }
  });

  // cache for current subscriber state
  List<SubscriberUpdate> _contextSubscribersCache = [];

  // connection to context debug
  SubscriberListenerBinding _contextDebugBinding;

  SendWebSocketMessage _sendMessage;

  @override
  void init(ApplicationContext appContext, SendWebSocketMessage sender) {
    this._sendMessage = sender;

    final contextDebug = new ContextDebugProxy();
    _contextDebugBinding = new SubscriberListenerBinding();
    connectToService(appContext.environmentServices, contextDebug.ctrl);
    assert(contextDebug.ctrl.isBound);

    // Watch subscription changes.
    contextDebug.watchSubscribers(_contextDebugBinding.wrap(this));
    contextDebug.ctrl.close();
  }

  @override
  bool handleRequest(String requestString, HttpRequest request) {
    return false;
  }

  @override
  void handleNewWebSocket(WebSocket socket) {
    // send all cached context data to the socket
    String message =
        JSON.encode({"context.subscribers": _contextSubscribersCache});
    socket.add(message);
  }

  @override
  void onUpdate(List<SubscriberUpdate> subscriptions) {
    _contextSubscribersCache = subscriptions;
    final String message = JSON.encode({"context.subscribers": subscriptions});
    this._sendMessage(message);
  }
}
