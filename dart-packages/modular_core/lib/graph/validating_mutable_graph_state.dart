// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:collection/collection.dart';

import 'delegating_graph_state.dart';
import 'graph.dart';
import 'mutable_graph_state.dart';
import 'mutation.dart';

class ValidatingMutableGraphState extends DelegatingGraphState
    with MutableGraphState {
  final MutableGraphState state;
  final bool allowRedundantMutations;

  ValidatingMutableGraphState(
      this.state, {this.allowRedundantMutations: true});

  /// Return true if the mutation is valid.  Throw an exception if the mutation
  /// is invalid.  If the mutation is redundant, either return false or throw
  /// an exception, depending on the value of [allowRedundantMutations].
  bool applyMutation(final GraphMutation mutation) {
    switch (mutation.type) {
      case GraphMutationType.addNode:
        if (containsNodeId(mutation.nodeId)) {
          if (!allowRedundantMutations)
            throw new FailedGraphMutation(mutation, errorString:
                'Attempt to add a node with ID that is already in use.');
          return false;
        }
        break;
      case GraphMutationType.removeNode:
        if (!containsNodeId(mutation.nodeId)) {
          if (!allowRedundantMutations)
            throw new FailedGraphMutation(mutation, errorString:
                'Attempt to remove a node that does not exist.');
          return false;
        }
        final List<EdgeId> inOutEdgeIds = <EdgeId>[];
        inOutEdgeIds.addAll(inEdgeIds(mutation.nodeId));
        inOutEdgeIds.addAll(outEdgeIds(mutation.nodeId));
        if (inOutEdgeIds.isNotEmpty) {
          throw new FailedGraphMutation(mutation, errorString:
              'Attempt to remove a node that is still referenced by edges. '
              '($inOutEdgeIds)');
        }
        break;
      case GraphMutationType.addEdge:
        if (containsEdgeId(mutation.edgeId)) {
          _checkEdgeOriginTargetAndLabels(mutation);
          // No conflict; the mutation is merely redundant.
          return false;
        }
        // Edge doesn't exist.  Validate preconditions before applying it.
        if (!containsNodeId(mutation.originNodeId))
          throw new MissingOriginNode(mutation);
        if (!containsNodeId(mutation.targetNodeId))
          throw new MissingTargetNode(mutation);
        break;
      case GraphMutationType.removeEdge:
        if (!containsEdgeId(mutation.edgeId)) {
          if (!allowRedundantMutations)
            throw new FailedGraphMutation(mutation, errorString:
                'Attempt to remove an edge that does not exist.');
          return false;
        }
        _checkEdgeOriginTargetAndLabels(mutation);
        break;
      case GraphMutationType.setValue:
        final NodeId nodeId = mutation.nodeId;
        if (!containsNodeId(nodeId)) throw new MissingSetValueNode(mutation);
        if (const ListEquality()
            .equals(mutation.newValue, getValue(nodeId, mutation.valueKey)))
          return false;
        break;
    }
    return state.applyMutation(mutation);
  }

  void _checkEdgeOriginTargetAndLabels(GraphMutation mutation) {
    final EdgeId edgeId = mutation.edgeId;
    final NodeId originId = origin(edgeId);
    final NodeId targetId = target(edgeId);
    if (mutation.originNodeId != originId && originId != null)
      throw new ConflictingOriginNode(mutation, originId);
    if (mutation.targetNodeId != targetId && originId != null)
      throw new ConflictingTargetNode(mutation, targetId);
    if (!const ListEquality().equals(mutation.labels, labels(edgeId)))
      throw new ConflictingEdgeLabels(mutation, labels(edgeId));
  }
}
