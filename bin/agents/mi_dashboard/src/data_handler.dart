// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:lib.app.dart/app.dart';

/// Signature for method that sends WebSocket messages
typedef void SendWebSocketMessage(String message);

abstract class DataHandler {
  /// Provide a name for this data handler
  String get name;

  /// Initialize the data handler
  void init(ApplicationContext appContext, SendWebSocketMessage sender);

  /// Handle an HTTP request for data from this handler
  bool handleRequest(String requestString, HttpRequest request);

  /// Handle the creation of a new WebSocket
  /// e.g., Send the socket a complete collection of all data
  void handleNewWebSocket(WebSocket socket);
}
