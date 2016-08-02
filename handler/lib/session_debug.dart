// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:typed_data';

import 'package:handler/graph/session_graph.dart';
import 'package:handler/session.dart';
import 'package:modular/builtin_types.dart';
import 'package:modular_core/graph/graph.dart';
import 'package:parser/expression.dart';

/// Watch for session changes and print the session graph as JSON when changes
/// occur.
class SessionWatcher {
  final _ShorthandTranslator _translator;
  final SessionGraph _graph;

  SessionWatcher(Session session)
      : _translator = new _ShorthandTranslator(session.recipe.use),
        _graph = session.graph {
    _graph.addObserver((GraphEvent event) {
      print('-' * 20 + ' change ' + '-' * 20);
      printGraph();
    });
  }

  Map<String, dynamic> _nodeToJson(
      final Node node, final Map<NodeId, Map<String, dynamic>> seenNodes) {
    if (seenNodes.containsKey(node.id)) {
      // This node has already been serialized, we should just put a reference
      // to it here.
      final String idString = node.id.toString();
      // Make sure that the original serialization includes the id.
      seenNodes[node.id]['@id'] = idString;
      // Return a JSON-LD-like structure to reference that.
      return {'@id': idString};
    }
    final Map<String, dynamic> nodeJson = new Map<String, dynamic>();
    seenNodes[node.id] = nodeJson;
    for (final String key in node.valueKeys) {
      assert(!nodeJson.containsKey(key));
      Uint8List value = node.getValue(key);
      dynamic valueJson;
      if (key == BuiltinString.label) {
        valueJson = BuiltinString.read(value);
      } else if (key == BuiltinInt.label) {
        valueJson = BuiltinInt.read(value);
      } else {
        // TODO(ianloic): handle this case better.
        valueJson = BuiltinString.read(value);
      }
      nodeJson[_translator.shorthandFromUrl(key)] = valueJson;
    }
    for (final Edge e in node.outEdges) {
      final Map<String, dynamic> target = _nodeToJson(e.target, seenNodes);
      // The key is all of the edge labels joined by spaces. This is
      // consistent with path expressions and avoids adding needless cycles
      // all of the place.
      final String key = _translator.allShorthandsFromUrls(e.labels);
      if (nodeJson.containsKey(key)) {
        if (nodeJson[key] is! List) {
          nodeJson[key] = [nodeJson[key]];
        }
        nodeJson[key].add(target);
      } else {
        nodeJson[key] = target;
      }
    }
    return nodeJson;
  }

  void printGraph() {
    final encoder = new JsonEncoder.withIndent("  ");
    // As we serialize the tree track which Node IDs map to which JSON
    // structures so that we can represent references.
    final Map<NodeId, Map<String, dynamic>> seenNodes =
        new Map<NodeId, Map<String, dynamic>>();
    print(encoder.convert(_nodeToJson(_graph.root, seenNodes)));
  }
}

class SessionDataLoader {
  final _ShorthandTranslator _translator;
  final SessionGraph _graph;

  SessionDataLoader(Session session)
      : _translator = new _ShorthandTranslator(session.recipe.use),
        _graph = session.graph;

  void load(String jsonString) {
    final Map<String, dynamic> json = JSON.decode(jsonString);
    assert(json is Map);

    _graph.mutate((final GraphMutator m) {
      void addToNode(final Node node, final Map<String, dynamic> values) {
        values.forEach((final String key, final dynamic value) {
          assert(key is String);
          Node n = m.addNode();
          m.addEdge(
              _graph.root.id, _translator.allUrlsFromShorthands(key), n.id);
          // TODO(ianloic): arrays?
          if (value is String) {
            m.setValue(n.id, BuiltinString.label, BuiltinString.write(value));
          } else if (value is int) {
            m.setValue(n.id, BuiltinInt.label, BuiltinInt.write(value));
          } else if (value is Map) {
            addToNode(n, value);
          } else {
            print("Don't know what to do with $value in $jsonString");
            assert(false);
          }
        });
      }
      addToNode(_graph.root, json);
    });
  }
}

class _ShorthandTranslator {
  final Map<String, String> _urlToShorthand = {};
  final Map<String, String> _shorthandToUrl = {};

  _ShorthandTranslator(Uses uses) {
    uses.shorthand.forEach((final String shorthand, final Label label) {
      final String url = label.uri.toString();
      _urlToShorthand[url] = shorthand;
      _shorthandToUrl[shorthand] = url;
    });
  }

  String urlFromShorthand(String shorthand) => _shorthandToUrl[shorthand];
  List<String> allUrlsFromShorthands(String shorthands) =>
      shorthands.split(' ').map(urlFromShorthand).toList();

  String shorthandFromUrl(String url) =>
      _urlToShorthand.containsKey(url) ? _urlToShorthand[url] : url;
  String allShorthandsFromUrls(Iterable<String> urls) =>
      urls.map(shorthandFromUrl).join(' ');
}
