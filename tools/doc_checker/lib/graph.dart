// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This makes the export method more readable.
// ignore_for_file: cascade_invocations

/// Utility to create and export simple directed graphs.
class Graph {
  final Map<String, Node> _nodes = <String, Node>{};
  final Map<Node, Set<Node>> _edges = <Node, Set<Node>>{};

  int _nextId = 0;
  Node _root;

  int get nodeCount => _nodes.length;

  /// Returns or creates a node with the given [label].
  Node getNode(String label) =>
      _nodes.putIfAbsent(label, () => Node._internal(label, _nextId++));

  /// Sets the graph's root node.
  set root(Node node) {
    if (!_nodes.containsValue(node)) {
      throw Exception('Unknown node: $node');
    }
    _root = node;
  }

  /// Inserts a new edge.
  void addEdge({Node from, Node to}) =>
      _edges.putIfAbsent(from, () => <Node>{}).add(to);

  /// Removes and returns all singletons from the graph.
  List<Node> removeSingletons() {
    final List<Node> singletons = _nodes.values
        .where((Node node) =>
            !_edges.containsKey(node) &&
            _edges.values.every((Set<Node> nodes) => !nodes.contains(node)))
        .toList();
    for (Node node in singletons) {
      _nodes.remove(node.label);
    }
    return singletons;
  }

  /// Returns the nodes which do not have a parent.
  List<Node> get orphans => _nodes.values
      .where((Node node) =>
          node != _root &&
          _edges.values.every((Set<Node> nodes) => !nodes.contains(node)))
      .toList();

  /// Computes the depth of each node starting on the root.
  Map<Node, int> _computeDepths() {
    final Map<Node, int> levels = <Node, int>{};
    void addLevel(int level, List<Node> nodes) {
      if (nodes.isEmpty) {
        return;
      }
      final List<Node> next = <Node>[];
      for (Node node in nodes) {
        if (levels.containsKey(node)) {
          continue;
        }
        levels[node] = level;
        if (_edges.containsKey(node)) {
          next.addAll(_edges[node]);
        }
      }
      addLevel(level + 1, next);
    }

    addLevel(0, <Node>[_root]);
    return levels;
  }

  /// Creates a string representation of this graph in the DOT format.
  void export(String name, StringSink out) {
    const List<String> colors = <String>[
      '#4285f4',
      '#f4b400',
      '#0f9d58',
      '#db4437',
    ];
    final Map<Node, int> levels = _computeDepths();
    out
      ..writeln('digraph $name {')
      ..writeln('edge [arrowhead="none", arrowtail="none"];');
    for (Node n in _nodes.values) {
      final String color =
          levels.containsKey(n) ? colors[levels[n] % colors.length] : '#ffffff';
      out.writeln(
          '${n.id} [label="${n.label}", style="filled", fillcolor="$color"];');
    }
    if (_root != null) {
      out.writeln('root=${_root.id};');
    }
    for (Node from in _edges.keys) {
      for (Node to in _edges[from]) {
        if (!levels.containsKey(from) ||
            !levels.containsKey(to) ||
            levels[from] < levels[to]) {
          out.writeln('${from.id} -> ${to.id};');
        }
      }
    }
    out.writeln('}');
  }
}

/// A node in the graph.
class Node {
  /// The node's id.
  final int id;

  /// The node's label.
  final String label;

  Node._internal(this.label, this.id);

  @override
  String toString() => label;

  @override
  bool operator ==(Object other) => other is Node && other.id == id;

  @override
  int get hashCode => id;
}
