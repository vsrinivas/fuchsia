// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:typed_data';

import 'id.dart';
import 'graph.dart';

typedef Node NodeRefFactory(NodeId id);

/// A [Node] is a lightweight reference object that allows easy access
/// to logical properties of a node on a [Graph].
///
/// If [isDeleted], all calls other than [id] will return null or an empty list.
abstract class Node {
  /// Returns true if the Node with [id] does not exist in the Graph which this
  /// references.
  bool get isDeleted;

  NodeId get id;

  /// Return all [Edge]s that originate at this [Node].
  Iterable<Edge> get outEdges;

  /// Return all [Edge]s that target this [Node].
  Iterable<Edge> get inEdges;

  Iterable<String> get valueKeys;
  Uint8List getValue(String key);

  Iterable<Edge> outEdgesWithLabels(Iterable<String> labels);
  Edge singleOutEdgeWithLabels(Iterable<String> labels);
  Iterable<Edge> inEdgesWithLabels(Iterable<String> labels);
  Edge singleInEdgeWithLabels(Iterable<String> labels);

  @override // Object
  String toString() => 'ref:$id';

  @override // Object
  bool operator ==(other) => other is Node && other.id == id;

  @override // Object
  int get hashCode => id.hashCode;

  dynamic toJson() => {
        'id': id.toString(),
        'values': new Map.fromIterable(valueKeys,
            key: (String key) => key,
            value: (String key) => BASE64.encode(getValue(key)))
      };
}

/// An [Edge] is a lightweight data object that describes an edge from a source
/// [Node] to a target [Node].
class Edge {
  final EdgeId id;
  final Node origin;
  final Node target;
  final Iterable<String> labels;

  Edge(this.id, this.origin, this.target, this.labels) {
    assert(origin != null);
    assert(target != null);
    assert(labels != null);
  }

  factory Edge.fromState(
          EdgeId id, GraphState state, NodeRefFactory nodeRefFactory) =>
      new Edge(id, nodeRefFactory(state.origin(id)),
          nodeRefFactory(state.target(id)), state.labels(id));

  @override
  String toString() => '$id';

  @override // Object
  bool operator ==(other) => other is Edge && other.id == id;

  @override // Object
  int get hashCode => id.hashCode;

  dynamic toJson() => {
        'id': id.toString(),
        'origin': origin.id.toString(),
        'target': target.id.toString(),
        'labels': labels.toList(),
      };
}
