// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'package:application.lib.app.dart/app.dart';
import 'package:apps.maxwell.services.context/context_provider.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';
import 'package:path/path.dart' as path;

const _configDir = "/system/data/mi_dashboard";
const _configFilename = "dashboard.config";
const _defaultWebrootPath = "webroot";
const _defaultPort = 5000;

const _portPropertyName = "port";
const _webrootPropertyName = "webroot";

int _port = _defaultPort;
var _webrootPath = _defaultWebrootPath;
Directory _webrootDirectory;

// TODO(jwnichols): Make sure all the relevant topics are listed here
// Eventually we may want a method to ContextProvider that subscribes to all
// topics.
final _topics = ["/modular_state"];
final _contextCache = new Map<String,String>();

final _contextProvider = new ContextProviderProxy();
final _contextListener = new _ContextListenerImpl();

void main(List args) {
  // Get a handle to the ContextProvider service
  final app_context = new ApplicationContext.fromStartupInfo();
  connectToService(app_context.environmentServices, _contextProvider.ctrl);
  assert(_contextProvider.ctrl.isBound);
  app_context.close();

  // Subscribe to the topics in |_topics|.
  ContextQuery query = new ContextQuery();
  query.topics = _topics;
  _contextProvider.subscribe(query, _contextListener.getHandle());

  // Read the config file from disk
  var configFile = new File(path.join(_configDir,_configFilename));
  configFile.readAsString(encoding: ASCII)
    .then(parseConfigAndStart);
}

class _ContextListenerImpl extends ContextListener {
  final ContextListenerBinding _binding = new ContextListenerBinding();

  _ContextListenerImpl();

  InterfaceHandle<ContextListener> getHandle() => _binding.wrap(this);

  @override
  void onUpdate(ContextUpdate update) {
    // Cache all context values that we receive
    update.values.forEach((String key, String value) {
      // print("[DASHBOARD UPDATE] ${key}: ${value}");
      _contextCache[key] = value;
    });
  }
}

void parseConfigAndStart(var configString) {
  // parse config file as JSON
  Map configMap = JSON.decode(configString);

  // port property
  if (configMap.containsKey(_portPropertyName))
    _port = configMap[_portPropertyName];

  // webroot property
  if (configMap.containsKey(_webrootPropertyName))
    _webrootPath = configMap[_webrootPropertyName];
  _webrootDirectory = new Directory(path.join(_configDir,_webrootPath));

  // Start the server
  print("[INFO] Starting MI Dashboard web server on port ${_port}...");
  HttpServer
      .bind(InternetAddress.ANY_IP_V6, _port)
      .then((server) {
        server.listen(handleRequest);
      });
}

void handleRequest(HttpRequest request) {
  // Identify requests requiring return of context data
  // Such requests will begin with /data/<service>/...
  var dataRequestPattern = new RegExp("/data/([^/]+)(/.+)");
  var match = dataRequestPattern.firstMatch(request.requestedUri.path);
  if (match != null) {
    var serviceName = match.group(1); // first match group is the service name
    // print("Returning data for service ${serviceName}");

    // we are returning JSON
    request.response.headers.contentType
      = new ContentType("application", "json", charset: "utf-8");

    // Figure out what service data to return
    switch(serviceName) {
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
    var requestPath = "${_webrootDirectory.path}/${request.requestedUri.path}";
    if (requestPath.endsWith('/'))
      requestPath += "index.html";
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

sendFile(File requestFile, HttpResponse response) async {
  // Set the content type correctly based on the file name suffix
  // The content type is text/plain if the suffix isn't identified
  if (requestFile.path.endsWith("html")) {
    response.headers.contentType
      = new ContentType("text", "html", charset: "utf-8");
  } else if (requestFile.path.endsWith("json")) {
    response.headers.contentType
      = new ContentType("application", "json", charset: "utf-8");
  } else if (requestFile.path.endsWith("js")) {
    response.headers.contentType
      = new ContentType("application", "javascript", charset: "utf-8");
  } else if (requestFile.path.endsWith("css")) {
    response.headers.contentType
      = new ContentType("text", "css", charset: "utf-8");
  } else if (requestFile.path.endsWith("jpg") ||
             requestFile.path.endsWith("jpeg")) {
    response.headers.contentType
      = new ContentType("image", "jpeg");
  } else if (requestFile.path.endsWith("png")) {
    response.headers.contentType
      = new ContentType("image", "png");
  } else {
    response.headers.contentType
      = new ContentType("text", "plain", charset: "utf-8");
  }

  // Send the contents of the file
  await requestFile.openRead().pipe(response);

  response.close();
}

void send404(HttpResponse response) {
  response.statusCode = 404;
  response.reasonPhrase = "File not found.";
  response.close();
}
