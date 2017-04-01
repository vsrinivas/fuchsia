// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'package:application.lib.app.dart/app.dart';
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

void main(List args) {
  // Read the config file from disk
  var configFile = new File(path.join(_configDir,_configFilename));
  configFile.readAsString(encoding: ASCII)
    .then(parseConfigAndStart);
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
  // TODO(jwnichols): Identify requests requiring return of dynamic data
  // Probably /data/... requests will return JSON data from the context service

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
      }

      // if we didn't return before now, send an error response
      send404(request.response);
  });
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
