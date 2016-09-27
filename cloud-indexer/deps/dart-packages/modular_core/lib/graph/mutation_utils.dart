// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:collection';

import 'graph.dart';
import 'mutation.dart';

/// Coalesces all mutations append to it so that it can offers a coherent,
/// equivalent list of mutations.
class CoalescedMutations {
  // List of all mutations in the coalescence.
  final LinkedList<_MutationLinkedListEntry> _mutations =
      new LinkedList<_MutationLinkedListEntry>();

  // Map of all structural mutations (node and edge addition and removal) in
  // the coalescence indexed by type and element id.
  final Map<GraphMutationType, Map<String, _MutationLinkedListEntry>>
      _structuralMutations =
      <GraphMutationType, Map<String, _MutationLinkedListEntry>>{};
  // Map of all values mutations in
  // the coalescence indexed by node id and label.
  final Map<NodeId, Map<String, _MutationLinkedListEntry>> _valuesMutations =
      <NodeId, Map<String, _MutationLinkedListEntry>>{};

  final List<GraphMutationType> _structuralMutationType = [
    GraphMutationType.addNode,
    GraphMutationType.addEdge,
    GraphMutationType.removeEdge,
    GraphMutationType.removeNode
  ];

  CoalescedMutations() {
    clear();
  }

  void appendMutation(GraphMutation mutation) {
    switch (mutation.type) {
      case GraphMutationType.addNode:
        {
          final _MutationLinkedListEntry removedNode =
              _structuralMutations[GraphMutationType.removeNode]
                  .remove(mutation.nodeId.toString());
          if (removedNode == null) {
            _structuralMutations[GraphMutationType.addNode]
                [mutation.nodeId.toString()] = _appendMutation(mutation);
          } else {
            removedNode.unlink();
          }
        }
        break;
      case GraphMutationType.removeNode:
        {
          final _MutationLinkedListEntry addedNode =
              _structuralMutations[GraphMutationType.addNode]
                  .remove(mutation.nodeId.toString());
          if (addedNode == null) {
            _structuralMutations[GraphMutationType.removeNode]
                [mutation.nodeId.toString()] = _appendMutation(mutation);
          } else {
            addedNode.unlink();
            final Map<String, _MutationLinkedListEntry> valuesMutations =
                _valuesMutations.remove(mutation.nodeId);
            if (valuesMutations != null) {
              valuesMutations.values
                  .forEach((_MutationLinkedListEntry entry) => entry.unlink());
            }
          }
        }
        break;
      case GraphMutationType.addEdge:
        {
          final _MutationLinkedListEntry removedEdge =
              _structuralMutations[GraphMutationType.removeEdge]
                  .remove(mutation.edgeId.toString());
          if (removedEdge == null) {
            _structuralMutations[GraphMutationType.addEdge]
                [mutation.edgeId.toString()] = _appendMutation(mutation);
          } else {
            removedEdge.unlink();
          }
        }
        break;
      case GraphMutationType.removeEdge:
        {
          final _MutationLinkedListEntry addedEdge =
              _structuralMutations[GraphMutationType.addEdge]
                  .remove(mutation.edgeId.toString());
          if (addedEdge == null) {
            _structuralMutations[GraphMutationType.removeEdge]
                [mutation.edgeId.toString()] = _appendMutation(mutation);
          } else {
            addedEdge.unlink();
          }
        }
        break;
      case GraphMutationType.setValue:
        {
          final Map<String, _MutationLinkedListEntry> valuesMutations =
              _valuesMutations.putIfAbsent(
                  mutation.nodeId, () => <String, _MutationLinkedListEntry>{});
          valuesMutations[mutation.valueKey]?.unlink();
          // CoalescedMutations is independent of any graph data, so setValue
          // mutations can't be coalesced to nothing (i.e. a sequence of
          // setValue mutations that collectively forms a noop will still be
          // coalesced into a single mutation).
          valuesMutations[mutation.valueKey] = _appendMutation(
              new GraphMutation(GraphMutationType.setValue,
                  nodeId: mutation.nodeId,
                  valueKey: mutation.valueKey,
                  newValue: mutation.newValue,
                  tags: mutation.tags));
        }
        break;
      default:
        assert(false);
    }
  }

  bool get isEmpty => _mutations.isEmpty;

  void clear() {
    for (GraphMutationType type in _structuralMutationType) {
      _structuralMutations[type] = <String, _MutationLinkedListEntry>{};
    }
    _valuesMutations.clear();
    _mutations.clear();
  }

  List<GraphMutation> toList() => _mutations
      .map((_MutationLinkedListEntry entry) => entry.mutation)
      .toList();

  _MutationLinkedListEntry _appendMutation(GraphMutation mutation) {
    final _MutationLinkedListEntry entry =
        new _MutationLinkedListEntry(mutation);
    _mutations.add(entry);
    return entry;
  }
}

class _MutationLinkedListEntry
    extends LinkedListEntry<_MutationLinkedListEntry> {
  final GraphMutation mutation;
  _MutationLinkedListEntry(this.mutation);
}
