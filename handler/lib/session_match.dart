// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:modular_core/graph/graph.dart' show Edge, Node, EdgeId;
import 'package:parser/expression.dart' show Property, PathExpr;

/// A match of a path expression against a graph is a set of edges, leading to
/// one or more target nodes. There may be multiple target nodes if there is a
/// repeated property in the path expression. There is at most one target node
/// if there is no repeated property in the path expression.
///
/// If a path expression matches multiple times, each match is represented by a
/// separate instance of SessionMatch, all of them held together in a common
/// SessionPattern instance. During matching, which is executed in
/// SessionPattern, such new instances are created using the partialClone()
/// method.
class SessionMatch {
  /// The expression to match.
  final PathExpr pathExpr;

  /// The inner dimension of the nested List is for matching repeated properties
  /// in the path expression.
  ///
  /// The outer dimension is for matches of path expressions with multiple
  /// properties.
  ///
  /// E.g., the expression `foo -> bar -> baz` would yield _edges of length
  /// three, with at most one Edge in each because no edge of the expression is
  /// repeated.
  final List<List<Edge>> _edges;

  /// Dirty flag to indicate this session match contains changes that have not
  /// been sent to a module implementation yet. Changes are added or deleted
  /// edges or changed node values.
  bool _hasNewData = true;

  SessionMatch(final PathExpr pathExpr)
      : this.pathExpr = pathExpr,
        _edges = new List<List<Edge>>(pathExpr.length) {
    for (int i = 0; i < pathExpr.length; ++i) {
      _edges[i] = <Edge>[];
    }
  }

  void copyFrom(final SessionMatch other) {
    assert(pathExpr == other.pathExpr);
    assert(length == other.length);
    for (int i = 0; i < length; ++i) {
      _edges[i].addAll(other._edges[i]);
    }
    _hasNewData = other._hasNewData;
  }

  /// Only for handler_test.dart.
  Node target(final int level, final int index) {
    final List<Node> targetLevel = targetList(level);
    if (targetLevel.length > index) {
      return targetLevel[index];
    }
    return null;
  }

  /// Targets of the expression are the nodes at the end of the edges matched by
  /// the last segment of the path expression.
  Iterable<Node> get targets => _edges.last.map((Edge e) => e.target);

  /// Only for handler_test.dart.
  List<Node> targetList(final int level) {
    return _edges[level].map((final Edge edge) => edge.target).toList();
  }

  /// Only for handler_test.dart and session_pattern_test.dart.
  List<Edge> edgeList(final int level) {
    if (_edges[level] == null) {
      _edges[level] = <Edge>[];
    }
    return _edges[level];
  }

  Iterable<Edge> get matchedEdges sync* {
    for (final List<Edge> edgeLevel in _edges) {
      if (edgeLevel != null) {
        yield* edgeLevel;
      }
    }
  }

  /// Clones this match up to the given path index. If a new edge matches at
  /// that level, and the expression is not repeated at that level, then this
  /// allows to create a new SessionMatch instance to which the new edge can be
  /// addEdge()ed.
  SessionMatch partialClone(final int pathIndex) {
    final SessionMatch newMatch = new SessionMatch(pathExpr);
    for (int i = 0; i < pathIndex; ++i) {
      newMatch._edges[i].addAll(_edges[i]);
    }
    return newMatch;
  }

  /// If the edges matched by this and the other session match are equal at
  /// every step in the path expression up to the length of this session match,
  /// then it is a prefix of the other.
  bool isPrefixOf(final SessionMatch o) {
    if (length > o.length) {
      return false;
    }

    for (int i = 0; i < length; ++i) {
      if (_edges[i].length != o._edges[i].length) {
        return false;
      }
      for (int j = 0; j < _edges[i].length; ++j) {
        if (_edges[i][j] != o._edges[i][j]) {
          return false;
        }
      }
    }

    return true;
  }

  /// Adds a single edge to the current matched set.
  void addEdge(final int pathIndex, final Edge newEdge) {
    _edges[pathIndex].add(newEdge);
    _hasNewData = true;
  }

  /// Removes the provided edge from the match. Returns true if the edge was
  /// present and has been removed. Also removes edges in subsequent steps
  /// connected only to the deleted edge.
  bool removeEdge(final EdgeId edgeId) {
    bool removed = false;

    // We need the level index for pruning, hence no Iterator.
    for (int ii = 0; ii < pathExpr.length; ++ii) {
      final List<Edge> step = _edges[ii];

      // We modify step, hence no Iterator.
      for (int i = 0; i < step.length; ++i) {
        final Edge edge = step[i];

        if (edge.id == edgeId) {
          step.removeAt(i--);
          _prune(ii + 1);
          removed = true;
        }
      }
    }

    if (removed) {
      _hasNewData = true;
    }

    return removed;
  }

  /// Marks this session match dirty if it contains the given node.
  void touchNode(final Node node) {
    for (final List<Edge> step in _edges) {
      for (final Edge edge in step) {
        if (edge.target == node) {
          _hasNewData = true;
          return;
        }
      }
    }
  }

  /// Removes all paths that do not start in the anchor set.
  void pruneToAnchorSet(final Set<Node> anchorSet, final bool footprint) {
    final List<Edge> step = _edges[0];
    for (int i = 0; i < step.length; ++i) {
      final Edge edge = step[i];
      if (!anchorSet.contains(footprint ? edge.target : edge.origin)) {
        step.removeAt(i--);
        _prune(1);
        _hasNewData = true;
      }
    }
  }

  /// Removes disconnected edges starting at the given level.
  void _prune(final int level) {
    for (int ii = level; ii < pathExpr.length; ++ii) {
      if (ii == 0) {
        continue;
      }
      final List<Edge> step = _edges[ii];
      for (int i = 0; i < step.length; ++i) {
        final Edge edge = step[i];
        if (!_edges[ii - 1].any((Edge e) => e.target == edge.origin)) {
          step.removeAt(i--);
        }
      }
    }
  }

  /// A session match is empty if it has no root edges.
  bool get isEmpty {
    return _edges[0].length == 0;
  }

  /// Checks whether the session match is complete, i.e. has an edge for every
  /// required step in the path expression. Does not check whether a required
  /// edge is connected to a preceding edge, because this invariant is
  /// maintained in addEdge/removeEdge.
  bool get isComplete {
    for (int i = 0; i < pathExpr.length; ++i) {
      final Property property = pathExpr.properties[i];

      // Once a step in a path expression is optional, the remainder is
      // optional too.
      if (property.cardinality.isOptional) {
        return true;
      }

      // If a level is empty that is not optional, the match is not
      // complete.
      if (_edges[i].isEmpty) {
        return false;
      }
    }
    return true;
  }

  // The node(s) where the first matched edges start.
  List<Node> get scope {
    return (_edges[0] ?? <Edge>[])
        .map((final Edge edge) => edge.origin)
        .toList();
  }

  int get length => _edges.length;

  bool get hasNewData => _hasNewData;

  // Resets the dirty flag to indicate all new data has been processed.
  void markProcessed() {
    _hasNewData = false;
  }

  Map<String, dynamic> inspectorJSON() {
    return {
      'type': 'sessionMatch',
      'pathExpr': pathExpr.toString(),
      'edges': _edges.isNotEmpty ? _edges.last : [],
    };
  }

  @override
  String toString() {
    final StringBuffer ret = new StringBuffer();
    ret.writeln('');
    ret.writeln('');
    ret.writeln('$pathExpr: $isComplete');
    for (int i = 0; i < _edges.length; ++i) {
      ret.writeln('${pathExpr.properties[i]}');
      for (final Edge edge in _edges[i]) {
        ret.writeln('$edge');
      }
    }
    ret.writeln('');
    ret.writeln('');
    return ret.toString();
  }
}
