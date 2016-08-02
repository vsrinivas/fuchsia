// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:modular_core/log.dart';
import 'package:modular_core/graph/graph.dart';
import 'package:parser/expression.dart' show PathExpr;
import 'package:parser/manifest.dart';

export 'dart:io' show IOSink;

const int DEBUG_SERVER_PORT = 1842;

abstract class Inspectable {
  String get inspectorPath;
  Future<dynamic> inspectorJSON();
  Future<dynamic> onInspectorPost(dynamic json);
}

class InspectorJSONServer implements Inspectable {
  final Logger _log = log('Inspector');
  final Map<String, Inspectable> _inspectables = {};
  final Map<String, Set<WebSocket>> _listeners = {};

  @override
  final String inspectorPath = '/';
  @override
  Future<dynamic> onInspectorPost(dynamic json) async {}

  HttpServer requestServer;
  JsonEncoder _jsonEncoder;
  Object _unhandled;

  InspectorJSONServer() {
    _jsonEncoder = new JsonEncoder.withIndent('  ', _encodeUnknown);
    publish(this);
    listen();
  }

  @override
  Future<dynamic> inspectorJSON() async {
    return {'type': 'root', 'endpoints': _inspectables.keys.toList()};
  }

  Inspectable _inspectable(String requestPath) {
    if (_inspectables.containsKey(requestPath)) {
      return _inspectables[requestPath];
    }
    return null;
  }

  Future<Null> _handleWebSocketRequest(HttpRequest request, String path) async {
    // TODO(ianloic): check Origin header?
    WebSocket socket = await WebSocketTransformer.upgrade(request);
    if (!_listeners.containsKey(path)) {
      _listeners[path] = new Set<WebSocket>();
    }
    _listeners[path].add(socket);
    socket.done.then((_) {
      _log.info("WebSocket for $path closed.");
      _listeners[path].remove(socket);
    });
  }

  void _handlePostRequest(
      final HttpRequest request, final Inspectable inspectable) {
    UTF8.decodeStream(request).then((final String jsonString) async {
      final dynamic json = JSON.decode(jsonString);
      final dynamic result = await inspectable.onInspectorPost(json);
      final String resultString = JSON.encode(result);
      request.response.headers.set('content-type', 'application/json');
      request.response.write(resultString);
      request.response.close();
    }).catchError((final dynamic e, StackTrace stack) {
      print("Got error $e handling POST to ${inspectable.inspectorPath}");
      print("$stack");
      request.response.reasonPhrase = '$e';
      request.response.statusCode = 500;
      request.response.close();
    });
  }

  Future<Null> _handleRequest(
      final HttpRequest request, final Inspectable inspectable) async {
    String json;
    dynamic data;
    try {
      data = await inspectable.inspectorJSON();
      _unhandled = null;
      json = _jsonEncoder.convert(data);
    } catch (error, trace) {
      _log.info('While serializing: ${data.runtimeType} $data');
      _log.info('encountered: ${_unhandled.runtimeType} $_unhandled');
      _log.info('$error');
      _log.info('$trace');
      if (_unhandled != null) {
        _log.severe(
            'Unhandled ${_unhandled.runtimeType} for JSON serialization:');
        _log.severe('$_unhandled');
      }
      _log.severe('Exception serving request.', error, trace);
      request.response.statusCode = 500;
      request.response.headers.set('content-type', 'text/plain');
      if (_unhandled != null) {
        request.response.writeln(
            'Unhandled ${_unhandled.runtimeType} for JSON serialization:');
        request.response.writeln('$_unhandled');
      }
      request.response.writeln('Exception: $error');
      request.response.write(trace.toString());
      request.response.close();
      return;
    }

    // Simple CORS support.
    final String originString = request.headers.value('origin');
    if (originString != null) {
      try {
        final Uri originUri = Uri.parse(originString);
        if (originUri.host == 'localhost') {
          request.response.headers
              .set('Access-Control-Allow-Origin', originString);
        } else {
          _log.warning('Ignoring CORS request from: $originString');
        }
      } on FormatException catch (_) {
        _log.warning('Ignoring invalid Origin header: $originString');
      }
    }

    request.response.headers.set('content-type', 'application/json');
    request.response.write(json);
    request.response.close();
  }

  Future<Null> listen() async {
    assert(requestServer == null);
    requestServer = await HttpServer.bind(
        InternetAddress.LOOPBACK_IP_V4, DEBUG_SERVER_PORT);
    _log.info('Handler Debug Server listening on port ${requestServer.port}');
    await for (HttpRequest request in requestServer) {
      _log.info("Handling request for ${request.uri.path}");

      if (request.uri.path.startsWith('/ws/')) {
        _handleWebSocketRequest(request, request.uri.path.substring(3));
        continue;
      }

      Inspectable inspectable = _inspectable(request.uri.path);
      if (inspectable == null) {
        request.response.statusCode = HttpStatus.NOT_FOUND;
        request.response.close();
        continue;
      }

      if (request.method == 'POST') {
        _handlePostRequest(request, inspectable);
        continue;
      }

      _handleRequest(request, inspectable);
    }
  }

  void publish(Inspectable object) {
    _inspectables[object.inspectorPath] = object;
    notify(this);
  }

  void publishGraph(final Graph graph, final String path) {
    publish(new _InspectableGraph(graph, path, this));
  }

  void unpublish(String path) {
    _inspectables.remove(path);
    // TODO(ianloic): close listeners.
    notify(this);
  }

  Future<Null> notify(Inspectable object) async {
    String path = object.inspectorPath;
    _log.info("Notifying update on $path");
    // TODO(ianloic): debounce / rate-limit notifications?
    // TODO(ianloic): track last message & only send if it actually changed.
    if (_listeners.containsKey(path) && _listeners[path].isNotEmpty) {
      dynamic data = await object.inspectorJSON();
      push(path, data);
    }
  }

  void push(final String path, final dynamic json) {
    String data = _jsonEncoder.convert(json);
    for (WebSocket socket in _listeners[path] ?? <WebSocket>[]) {
      socket.add(data);
    }
  }

  String dataUri(Uint8List data) => 'data:;base64;' + BASE64.encode(data);

  Map<String, dynamic> edge(Edge edge) => {
        'type': 'edge',
        'id': edge.id,
        'origin': edge.origin.id,
        'target': edge.target.id,
        'labels': edge.labels.toList(),
      };

  Map<String, dynamic> node(Node node) => {
        'type': 'node',
        'id': node.id,
        'values': new Map<String, String>.fromIterable(node.valueKeys,
            value: (String label) => dataUri(node.getValue(label))),
      };

  Map<String, dynamic> graph(Graph graph) => {
        'type': 'graph',
        'nodes': _iterableWithIdsToMap(graph.nodes.map((Node n) => node(n))),
        'edges': _iterableWithIdsToMap(graph.edges.map((Edge e) => edge(e))),
      };

  Map<String, dynamic> manifest(Manifest manifest) {
    Iterable<String> pathExprs(Iterable<PathExpr> ps) =>
        ps.map((PathExpr p) => p.toString()) ?? [];
    String themeColor;
    if (manifest.themeColor != null) {
      themeColor = '#' + manifest.themeColor.toRadixString(16).padLeft(6, '0');
    }
    return {
      'title': manifest.title,
      'url': manifest.url,
      'icon': manifest.icon,
      'themeColor': themeColor,
      'verb': manifest.verb.toString(),
      'input': pathExprs(manifest.input),
      'output': pathExprs(manifest.output),
      'display': pathExprs(manifest.display),
      'compose': pathExprs(manifest.compose),
    };
  }

  // Given an Iterable of Maps with String ids, return a Map mapping the ids
  // to their Maps.
  Map<String, Map<String, dynamic>> _iterableWithIdsToMap(
          Iterable<Map<String, dynamic>> i) =>
      new Map<String, Map<String, dynamic>>.fromIterable(i,
          key: (Map<String, dynamic> m) => m['id'].toString());

  Object _encodeUnknown(Object o) {
    if (o is Inspectable) {
      return o.inspectorPath;
    }
    if (o is Uri || o is NodeId || o is EdgeId) {
      return o.toString();
    }
    if (o is Edge) {
      return edge(o);
    }
    if (o is Node) {
      return node(o);
    }
    if (o is Graph) {
      return graph(o);
    }
    if (o is Iterable<dynamic>) {
      return o.map(_encodeUnknown).toList();
    }
    _unhandled = o;
    return o;
  }
}

class _InspectableGraph implements Inspectable {
  final Graph graph;
  final String path;
  final InspectorJSONServer _inspector;

  _InspectableGraph(this.graph, this.path, this._inspector) {
    this.graph.addObserver(this.graphChanged);
  }

  @override
  Future<dynamic> inspectorJSON() => new Future.value(graph.toJson());

  String get inspectorPath => path;

  @override
  Future<dynamic> onInspectorPost(dynamic json) {
    if (json is! List) {
      throw new ArgumentError("Expected an array of mutations");
    }
    final Iterable<GraphMutation> mutations =
        (json as List).map((final dynamic m) => new GraphMutation.fromJson(m));
    graph.mutate((final GraphMutator gm) {
      for (final GraphMutation m in mutations) {
        gm.apply(m);
      }
    });
    return new Future.value({});
  }

  void graphChanged(GraphEvent event) => _inspector?.push(path, event.toJson());
}
