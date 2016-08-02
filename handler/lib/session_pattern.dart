// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:modular_core/entity/entity.dart' show Entity;
import 'package:modular_core/graph/graph.dart' show Edge, EdgeId, Node;
import 'package:parser/expression.dart' show Label, Property, PathExpr;

import 'module.dart' show SessionMatchUpdate;
import 'session_match.dart';

/// This class keeps track of the matches of a single path expression in the
/// graph. As the graph is modified, a single expression may yield one or more
/// matches, each represented by SessionMatch. The session matches that belong
/// to a single expression are related, and this class represents that
/// relationship.
///
/// A path expression may be matched against the graph relative to a set of
/// nodes called the anchor set. The anchor set is then shared between all
/// matches of the path expression. This class also handles the situation that a
/// change to the graph results in an update to the anchor set.
///
/// Path expressions are used by recipe steps and module manifests.
class SessionPattern {
  /// The expression whose matches to track.
  final PathExpr pathExpr;

  /// Whether this session pattern tracks a footprint. This relates to where the
  /// anchor set is relative to the first match: In a non-footprint pattern, the
  /// *origin* of the first edge has to be in the anchor set. In a footprint
  /// pattern, the *target* of the first edge has to be in the anchor set. This
  /// is so that non-footprint and footprint pattern can overlap on a shared
  /// anchor node, and so that the anchor node is identified by the same label
  /// in both patterns. In the simple and most usual case of a single-segment
  /// path expression, the expression as a footprint matches its own match as a
  /// non-footprint.
  final bool _footprint;

  /// The anchor set shared by all matches of the path expression. The target
  /// node of the first edge in each match must be in the anchor set. If the
  /// pattern tracks an expression in a recipe step, the anchor set is the
  /// session root node. If the pattern tracks an expression in a manifest, the
  /// anchor set is made from the leaves of the matches of the step expressions.
  final Set<Node> anchorSet;

  /// The matches that the tracked expression gives rise to in the session
  /// Graph. There might be multiple if the input expression is *not* repeated,
  /// yielding multiple separate matches. A repeated expression, by contrast,
  /// yields a *single* match, which contains all matching fragments of the
  /// graph.
  final List<SessionMatch> matches = <SessionMatch>[];

  /// Indicates whether the [SessionPattern] is newly created and has not seen
  /// any update so far.
  bool _firstUpdate = true;

  SessionPattern(this.pathExpr, final Iterable<Node> anchors,
      {final bool footprint: false})
      : _footprint = footprint,
        anchorSet = new Set<Node>.from(anchors) {
    // We always create an empty session match. If the path expression is
    // optional, this match is even complete.
    matches.add(new SessionMatch(pathExpr));
  }

  /// Adjusts the matches once the anchor set changes.
  void updateAnchorSet(final Iterable<Node> anchors) {
    anchorSet.clear();
    anchorSet.addAll(anchors);
    for (final SessionMatch match in matches) {
      match.pruneToAnchorSet(anchorSet, _footprint);
    }
  }

  /// Records a new SessionMatch in the matches collection.
  void _addMatch(final SessionMatch newMatch) {
    if (matches.first.isEmpty) {
      // This is the empty match created initially.
      matches.first.copyFrom(newMatch);
    } else {
      matches.add(newMatch);
    }
  }

  /// Deletes the edges in all the matches kept here as specified in the update
  /// data. See Module.deleteEdges() comment as to why this is done separately
  /// from updateMatches().
  void deleteEdges(final SessionMatchUpdate updateData) {
    for (final EdgeId edgeId in updateData.deletedEdgeIds) {
      for (final SessionMatch match in matches) {
        match.removeEdge(edgeId);
      }
    }
  }

  /// Updates the matches in one SessionPattern with edges and nodes.
  void updateMatches(final SessionMatchUpdate updateData) {
    final List<Edge> matchEdges =
        _firstUpdate ? updateData.allEdges : updateData.updatedEdges;
    _firstUpdate = false;
    for (final Edge edge in matchEdges) {
      _updateMatches(edge);
    }

    for (final Node node in updateData.modifiedNodes) {
      for (final SessionMatch match in matches) {
        match.touchNode(node);
      }
    }
  }

  /// Updates the matches in one SessionPattern with one edge.
  ///
  /// We add the new edge, supposed to be matched by the tracked expression, to
  /// the set of matched edges. There are 8 cases, the cartesian product of:
  ///
  /// 1. the edge is matched by the first path component or a later one,
  ///
  /// 2. the matching input expression prefix is repeated or not,
  ///
  /// 3. the resulting match is complete or partial.
  ///
  /// A match of a repeated expression may contain full and partial matches
  /// together.
  ///
  /// Example:
  ///
  /// input expression:
  ///
  ///   p1+ -> p2
  ///
  /// graph after init:
  ///
  ///   n1 --(p1)-> n2
  ///   n2 --(p2)-> n3
  ///
  /// update edge:
  ///
  ///   n1 --(p1)-> n4
  ///
  /// After update, the session match for the input has the target set [[n1],
  /// [n2, n4], [n3]], but n4 should probably not be accessible by the module
  /// instance.
  void _updateMatches(final Edge edge) {
    // Skip if the edge is already matched so we don't create duplicates. It is
    // important that this check is executed over all matches, not just by each
    // match separately.
    if (_matchedEdges.contains(edge)) {
      return;
    }

    final Set<String> matchLabelSet = edge.labels.toSet()
      ..addAll(Entity.typesFromNode(edge.target));

    bool repeated = false;
    for (int i = 0; i < pathExpr.length; ++i) {
      final Property property = pathExpr.properties[i];
      repeated = repeated || property.cardinality.isRepeated;

      final Iterable<String> propertyLabels =
          property.labels.map((final Label label) => label.uri.toString());

      // If the edge doesn't have the labels of the path expression, then it
      // won't match.
      if (!matchLabelSet.containsAll(propertyLabels)) {
        continue;
      }

      // If an edge matches here, it doesn't match again anywhere else, so we
      // stop trying.
      if (_updateMatchWithOneEdge(edge, i, repeated)) {
        break;
      }
    }
  }

  /// Possibly updates the one match at one path expression position with one
  /// edge. If the edge updates one match at one position, it won't update any
  /// other match or position in this SessionPattern. Thus it returns the fact
  /// whether it matched.
  bool _updateMatchWithOneEdge(
      final Edge edge, final int position, final bool repeated) {
    // If the edge matches the label of the input path expression at the first
    // segment, it matches, and we update the match set of the module input. The
    // start node of the edge is added to scope of the input.
    if (position == 0) {
      // The first path segment in the match must start in the anchor set in a
      // scope pattern, and end in the anchor set in a footprint pattern.
      if (!anchorSet.contains(_footprint ? edge.target : edge.origin)) {
        return false;
      }

      // If the expression is repeated at the first path segment, then there is
      // at most one match, and it's either partial or complete. If there is one
      // match, we just add the edge to it. If there is none, we create one.
      if (repeated) {
        matches[0].addEdge(0, edge);
      } else {
        _addMatch(new SessionMatch(pathExpr)..addEdge(0, edge));
      }

      return true;
    }

    // If the edge matches the label of the input path expression at a later
    // segment, we inspect all previous matches at that level to find one
    // containing the start node of the edge. We need to iterate over a copy of
    // matches, as we might modify the list during iteration.
    for (final SessionMatch match in matches.toList()) {
      if (match.targetList(position - 1).contains(edge.origin)) {
        if (repeated || match.targetList(position).isEmpty) {
          match.addEdge(position, edge);
        } else {
          _addMatch(match.partialClone(position)..addEdge(position, edge));
        }
        return true;
      }
    }

    return false;
  }

  /// Computes the set of all edges matched by all session matches under this
  /// instance. It's important that this set is computed across all matches, so
  /// that an edge that is matched by one match is not matched again by another
  /// match (which would then cause yet another match instance to be created).
  Set<Edge> get _matchedEdges {
    final Set<Edge> matchedEdges = new Set<Edge>();
    for (final SessionMatch match in matches) {
      matchedEdges.addAll(match.matchedEdges);
    }
    return matchedEdges;
  }

  /// Clears empty matches, except for the first. Resets the dirty flags on all
  /// matches.
  void clearMatches() {
    for (int i = matches.length - 1; i > 0; --i) {
      if (matches[i].isEmpty) {
        matches.removeAt(i);
      }
    }

    for (final SessionMatch match in matches) {
      match.markProcessed();
    }
  }

  /// A SessionPattern is complete if it has a match that is complete.
  bool get isComplete => matches.any((SessionMatch m) => m.isComplete);

  @override
  String toString() => "$runtimeType:$pathExpr/$matches";
}
