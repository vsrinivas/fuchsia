// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

import 'package:modular_core/graph/graph.dart' as graph;
import 'package:modular_core/graph/mutation.dart' as graph;
import 'package:modular_core/uuid.dart';

import '../../modular/graph.mojom.dart' as mojom;

/// Utilities for converting to and from Mojom and Dart GraphMutation types.
// TODO(armansito): Add unit tests

mojom.GraphMutation dartMutationToMojom(final graph.GraphMutation mutation) {
  assert(mutation != null);
  mojom.GraphMutation mojomMutation = new mojom.GraphMutation();

  switch (mutation.type) {
    case graph.GraphMutationType.addNode:
      mojomMutation.nodeAdded = new mojom.NodeMutation()
        ..nodeId = mutation.nodeId.toString();
      break;

    case graph.GraphMutationType.removeNode:
      mojomMutation.nodeRemoved = new mojom.NodeMutation()
        ..nodeId = mutation.nodeId.toString();
      break;

    case graph.GraphMutationType.addEdge:
      mojomMutation.edgeAdded = new mojom.EdgeMutation()
        ..edgeId = mutation.edgeId.toString()
        ..originNodeId = mutation.originNodeId.toString()
        ..targetNodeId = mutation.targetNodeId.toString()
        ..labels = mutation.labels;
      break;

    case graph.GraphMutationType.removeEdge:
      mojomMutation.edgeRemoved = new mojom.EdgeMutation()
        ..edgeId = mutation.edgeId.toString()
        ..originNodeId = mutation.originNodeId.toString()
        ..targetNodeId = mutation.targetNodeId.toString()
        ..labels = mutation.labels;
      break;

    case graph.GraphMutationType.setValue:
      mojomMutation.valueChanged = new mojom.NodeValueMutation()
        ..nodeId = mutation.nodeId.toString()
        ..key = mutation.valueKey
        ..newValue = mutation.newValue;
      break;

    default:
      throw 'Invalid mutation type: ${mutation.type}';
  }

  return mojomMutation;
}

graph.GraphMutation mojomMutationToDart(final mojom.GraphMutation mutation) {
  assert(mutation != null);
  switch (mutation.tag) {
    case mojom.GraphMutationTag.nodeAdded:
      assert(mutation.nodeAdded != null);
      return new graph.GraphMutation.addNode(
          new graph.NodeId.fromString(mutation.nodeAdded.nodeId));

    case mojom.GraphMutationTag.nodeRemoved:
      assert(mutation.nodeRemoved != null);
      return new graph.GraphMutation.removeNode(
          new graph.NodeId.fromString(mutation.nodeRemoved.nodeId));

    case mojom.GraphMutationTag.edgeAdded:
      assert(mutation.edgeAdded != null);
      return new graph.GraphMutation.addEdge(
          new graph.EdgeId.fromString(mutation.edgeAdded.edgeId),
          new graph.NodeId.fromString(mutation.edgeAdded.originNodeId),
          new graph.NodeId.fromString(mutation.edgeAdded.targetNodeId),
          new List.unmodifiable(mutation.edgeAdded.labels));

    case mojom.GraphMutationTag.edgeRemoved:
      assert(mutation.edgeRemoved != null);
      return new graph.GraphMutation.removeEdge(
          new graph.EdgeId.fromString(mutation.edgeRemoved.edgeId),
          new graph.NodeId.fromString(mutation.edgeRemoved.originNodeId),
          new graph.NodeId.fromString(mutation.edgeRemoved.targetNodeId),
          new List.unmodifiable(mutation.edgeRemoved.labels));

    case mojom.GraphMutationTag.valueChanged:
      final newValue = mutation.valueChanged.newValue == null
          ? null
          : new Uint8List.fromList(mutation.valueChanged.newValue);
      assert(mutation.valueChanged != null);
      return new graph.GraphMutation.setValue(
          new graph.NodeId.fromString(mutation.valueChanged.nodeId),
          mutation.valueChanged.key,
          newValue);
    default:
      break;
  }

  throw 'Malformed GraphMutation received over Mojo: $mutation';
}

mojom.GraphEvent dartEventToMojom(final graph.GraphEvent event) {
  assert(event != null && event.id != null);
  final result = new mojom.GraphEvent();
  result.id = event.id.toUint8List();
  result.mutations = event.mutations.map(dartMutationToMojom).toList();
  return result;
}

graph.GraphEvent mojomEventToDart(final mojom.GraphEvent event) {
  assert(event != null);
  final Uuid id = new Uuid(event.id);
  final List<graph.GraphMutation> mutations =
      event.mutations.map(mojomMutationToDart).toList();
  return new graph.GraphEvent(null, mutations, id: id);
}
