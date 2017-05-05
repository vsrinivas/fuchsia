// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:application.lib.app.dart/app.dart';
import 'package:apps.maxwell.lib.context.dart/context_listener_impl.dart';
import 'package:apps.maxwell.services.context/context_provider.fidl.dart';
import 'package:path/path.dart' as path;

const _configDir = "/system/data/mi_dashboard";
const _configFilename = "dashboard.config";
const _defaultWebrootPath = "webroot";
const _defaultPort = 4000;

const _portPropertyName = "port";
const _webrootPropertyName = "webroot";

int _port = _defaultPort;
var _webrootPath = _defaultWebrootPath;
Directory _webrootDirectory;

var _activeWebsockets = new List<WebSocket>();

final _contextCache = new Map<String, String>();
final _contextProvider = new ContextProviderProxy();
final _contextListener = new ContextListenerImpl((ContextUpdate update) {
  // Cache all context values that we receive
  update.values.forEach((String key, String value) {
    // print("[DASHBOARD UPDATE] ${key}: ${value}");
    _contextCache[key] = value;
  });

  // Send updates to all active websockets
  if (_activeWebsockets.length > 0) {
    String message = JSON.encode({"type": "context", "data": update.values});
    _activeWebsockets.forEach((socket) {
      socket.add(message);
    });
  }
});

void main(List args) {
  // Get a handle to the ContextProvider service
  final appContext = new ApplicationContext.fromStartupInfo();
  connectToService(appContext.environmentServices, _contextProvider.ctrl);
  assert(_contextProvider.ctrl.isBound);
  appContext.close();

  // Subscribe to the topics in |_topics|.
  ContextQuery query = new ContextQuery();
  query.topics = []; // empty list is the wildcard query
  _contextProvider.subscribe(query, _contextListener.getHandle());

  // Read the config file from disk
  var configFile = new File(path.join(_configDir, _configFilename));
  configFile.readAsString(encoding: ASCII).then(parseConfigAndStart);
}

void parseConfigAndStart(String configString) {
  // parse config file as JSON
  Map configMap = JSON.decode(configString);

  // port property
  if (configMap.containsKey(_portPropertyName))
    _port = configMap[_portPropertyName];

  // webroot property
  if (configMap.containsKey(_webrootPropertyName))
    _webrootPath = configMap[_webrootPropertyName];
  _webrootDirectory = new Directory(path.join(_configDir, _webrootPath));

  // Start the web server
  print("[INFO] Starting MI Dashboard web server on port ${_port}...");
  HttpServer.bind(InternetAddress.ANY_IP_V6, _port).then((server) {
    server.listen(handleRequest);
  });
}

void handleRequest(HttpRequest request) {
  // Identify websocket requests
  if (request.requestedUri.path.startsWith("/ws")) {
    WebSocketTransformer.upgrade(request).then((socket) {
      _activeWebsockets.add(socket);
      socket.listen(handleWebsocketRequest,
                    onDone: () { handleWebsocketClose(socket); });
      sendAllContextDataToWebsocket(socket);
    });
  } else {
    // Identify requests requiring return of context data
    // Such requests will begin with /data/<service>/...
    var dataRequestPattern = new RegExp("/data/([^/]+)(/.+)");
    var match = dataRequestPattern.firstMatch(request.requestedUri.path);
    if (match != null) {
      var serviceName = match.group(1); // first match group is the service name
      // print("Returning data for service ${serviceName}");

      // we are returning JSON
      request.response.headers.contentType =
          new ContentType("application", "json", charset: "utf-8");

      // Figure out what service data to return
      switch (serviceName) {
        case 'context':
          //   /data/context/<topic>
          //     return JSON data from the context service for the given topic
          var topic = match.group(2);
          var topicValue = _contextCache[topic];
          // print("[DASHBOARD] Request for context topic ${topic} with value ${topicValue}");
          if (topicValue != null) {
            // Write the data to the response.
            request.response.write(topicValue);
            request.response.close();
            return;
          }

        // TODO(jwnichols): Report data from other intelligence services. E.g.,
        //   /data/actionlog/...
        //   /data/suggestions/...
      }
      // Nothing handled the request, so respond with a 404
      send404(request.response);
    } else {
      // Find the referenced file
      // path.join does not work in this case, possibly because the request path
      // may start with a /, so using a simple string concatenation instead
      var requestPath =
          "${_webrootDirectory.path}/${request.requestedUri.path}";
      if (requestPath.endsWith('/')) requestPath += "index.html";
      var requestFile = new File(requestPath);
      requestFile.exists().then((exists) {
        if (exists) {
          // Make sure the referenced file is within the webroot
          if (requestFile.uri.path.startsWith(_webrootDirectory.path)) {
            sendFile(requestFile, request.response);
            return;
          }
        } else {
          send404(request.response);
        }
      });
    }
  }
}

Future sendFile(File requestFile, HttpResponse response) async {
  // Set the content type correctly based on the file name suffix
  // The content type is text/plain if the suffix isn't identified
  if (requestFile.path.endsWith("html")) {
    response.headers.contentType =
        new ContentType("text", "html", charset: "utf-8");
  } else if (requestFile.path.endsWith("json")) {
    response.headers.contentType =
        new ContentType("application", "json", charset: "utf-8");
  } else if (requestFile.path.endsWith("js")) {
    response.headers.contentType =
        new ContentType("application", "javascript", charset: "utf-8");
  } else if (requestFile.path.endsWith("css")) {
    response.headers.contentType =
        new ContentType("text", "css", charset: "utf-8");
  } else if (requestFile.path.endsWith("jpg") ||
      requestFile.path.endsWith("jpeg")) {
    response.headers.contentType = new ContentType("image", "jpeg");
  } else if (requestFile.path.endsWith("png")) {
    response.headers.contentType = new ContentType("image", "png");
  } else {
    response.headers.contentType =
        new ContentType("text", "plain", charset: "utf-8");
  }

  // Send the contents of the file
  await requestFile.openRead().pipe(response);

  return response.close();
}

void send404(HttpResponse response) {
  response.statusCode = 404;
  response.reasonPhrase = "File not found.";
  response.close();
}

void handleWebsocketRequest(String event) {
  print("[INFO] websocket event was received!");
}

void handleWebsocketClose(WebSocket socket) {
  print("[INFO] Websocket closed (${_activeWebsockets.indexOf(socket)})");
  _activeWebsockets.remove(socket);
}

void sendAllContextDataToWebsocket(WebSocket socket) {
  String message = JSON.encode({"type": "context", "data": _contextCache});
  socket.add(message);
}
