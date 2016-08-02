// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:typed_data';

import 'package:collection/collection.dart';

import 'id.dart';
import 'ref.dart';

export 'mutation_list.dart' show GraphMutationList;

/// An interface that allows for mutation of the structure of a [Graph].
///
/// Methods accept IDs instead of Node or Edge reference objects because they
/// are the lowest-level object that can uniquely identify a specific Node or
/// Edge. Accessing the ID from a Node object is a simple attribute getter,
/// while accessing a Node from an ID is a call to the [Graph] instance.
///
/// Validation errors when performing mutations throw a [FailedGraphMutation].
abstract class GraphMutator {
  /// Creates a new [Node] on the [Graph], and returns a reference to the new
  /// node.
  Node addNode();

  /// Removes [nodeId] from the [Graph], including all in-edges and all
  /// out-edges.
  void removeNode(NodeId nodeId);

  /// Sets value for [key] on [node] to [value]. If the value is null,
  /// then the key is removed from the node.
  void setValue(NodeId nodeId, String key, Uint8List value);

  /// Creates a new edge from [origin] to [target] with the given [labels].
  Edge addEdge(NodeId originId, Iterable<String> labels, [NodeId targetId]);

  /// Removes [edge] from the [Graph].
  void removeEdge(EdgeId edgeId);

  /// Applies the low-level [mutation] to the graph.
  void apply(GraphMutation mutation);
}

enum GraphMutationType {
  /// [GraphMutation].[nodeId] will return non-null for the following.
  addNode,
  removeNode,

  /// [GraphMutation].[edgeId], [originNodeId], [targetNodeId] and [labels] will
  ///  return non-null for the following.
  addEdge,
  removeEdge,

  /// [GraphMutation].[nodeId] and [valueKey] will return non-null for the
  /// following.
  setValue
}

/// Represents the smallest unit of change that can be applied to a [Graph].
///
/// [GraphMutation] objects are used as the ground-truth source of data for a
/// given [Graph], and can be used to reconstruct the state of the graph at any
/// time-point.
class GraphMutation {
  final DateTime creationTimestamp;
  final String creationClientId;

  final GraphMutationType type;

  final NodeId nodeId;
  final EdgeId edgeId;
  final NodeId originNodeId;
  final NodeId targetNodeId;
  final List<String> labels;
  final String valueKey;
  final Uint8List newValue;

  /// A set of tags that help clients identify where in code a mutation was
  /// applied. This helps clients ignore [GraphMutation]s that are part of a
  /// notification event that originated in their code.
  ///
  /// [tags] are meant to be sticky, so if this mutation is applied to other
  /// graphs to replicate changes, new tags should be added onto those already
  /// present.
  ///
  /// Tags are not preserved through serialization/deserialization.
  final Set<dynamic> tags;

  GraphMutation(this.type,
      {final DateTime timestamp,
      this.creationClientId,
      this.nodeId,
      this.edgeId,
      this.originNodeId,
      this.targetNodeId,
      this.labels,
      this.valueKey,
      this.newValue,
      final Set<dynamic> tags})
      : creationTimestamp = timestamp ?? new DateTime.now(),
        tags = tags ?? new Set<dynamic>() {
    if (!_validate()) {
      throw new ArgumentError("Invalid mutation created: $this");
    }
    // assert(_validate());
  }

  factory GraphMutation.fromJson(dynamic json) {
    if (json is! Map) {
      throw new ArgumentError("Invalid GraphMutation JSON: $json");
    }
    Map<String, dynamic> map = json;
    if (map['type'] is! String) {
      throw new ArgumentError("Invalid GraphMutation type ${map['type']}");
    }
    final String type = map['type'];

    NodeId nodeId(String key) {
      if (!map.containsKey(key)) {
        throw new ArgumentError('Missing node "$key".');
      }
      dynamic value = map[key];
      if (value is! String) {
        throw new ArgumentError('Node id for "$key" is not a string.');
      }
      return new NodeId.fromString(value);
    }

    EdgeId edgeId(String key) {
      if (!map.containsKey(key)) {
        throw new ArgumentError('Missing edge "$key".');
      }
      dynamic value = map[key];
      if (value is! String) {
        throw new ArgumentError('Edge id for "$key" is not a string.');
      }
      return new EdgeId.fromString(value);
    }

    List<String> labels() {
      if (map['labels'] is! List || map['labels'].any((l) => l is! String)) {
        throw new ArgumentError('Edge labels must me a list of strings.');
      }
      return map['labels'];
    }

    switch (type) {
      case 'addNode':
        return new GraphMutation.addNode(nodeId('node'));
      case 'removeNode':
        return new GraphMutation.removeNode(nodeId('node'));
      case 'addEdge':
        return new GraphMutation.addEdge(
            edgeId('edge'), nodeId('origin'), nodeId('target'), labels());
      case 'removeEdge':
        return new GraphMutation.removeEdge(
            edgeId('edge'), nodeId('origin'), nodeId('target'), labels());
      case 'setValue':
        if (map['key'] is! String) {
          throw new ArgumentError('setValue key is not a string');
        }
        Uint8List value = null;
        if (map.containsKey('value')) {
          dynamic jsonValue = map['value'];
          if (jsonValue is! String) {
            throw new ArgumentError('invalid setValue value $jsonValue');
          }
          try {
            value = BASE64.decode(jsonValue);
          } on FormatException catch (_) {
            throw new ArgumentError('invalid Base64 value: $jsonValue');
          }
        }
        return new GraphMutation.setValue(nodeId('node'), map['key'], value);
    }
    throw new ArgumentError("Unexpected GraphMutation type $type");
  }

  factory GraphMutation.addNode(NodeId nodeId) =>
      new GraphMutation(GraphMutationType.addNode, nodeId: nodeId);

  factory GraphMutation.removeNode(NodeId nodeId) =>
      new GraphMutation(GraphMutationType.removeNode, nodeId: nodeId);

  factory GraphMutation.addEdge(EdgeId edgeId, NodeId originNodeId,
          NodeId targetNodeId, Iterable<String> labels) =>
      new GraphMutation(GraphMutationType.addEdge,
          edgeId: edgeId,
          labels: new List<String>.from(labels),
          originNodeId: originNodeId,
          targetNodeId: targetNodeId);

  factory GraphMutation.removeEdge(EdgeId edgeId, NodeId originNodeId,
          NodeId targetNodeId, Iterable<String> labels) =>
      new GraphMutation(GraphMutationType.removeEdge,
          edgeId: edgeId,
          labels: new List<String>.from(labels),
          originNodeId: originNodeId,
          targetNodeId: targetNodeId);

  factory GraphMutation.setValue(
          NodeId nodeId, String valueKey, Uint8List newValue) =>
      new GraphMutation(GraphMutationType.setValue,
          nodeId: nodeId,
          valueKey: valueKey,
          newValue: newValue != null ? new Uint8List.fromList(newValue) : null);

  factory GraphMutation.fromMutationWithOverrides(final GraphMutation mutation,
      {dynamic tag}) {
    Set<dynamic> newTags = new Set<dynamic>.from(mutation.tags);
    if (tag != null) newTags.add(tag);
    return new GraphMutation(mutation.type,
        timestamp: mutation.creationTimestamp,
        creationClientId: mutation.creationClientId,
        nodeId: mutation.nodeId,
        edgeId: mutation.edgeId,
        originNodeId: mutation.originNodeId,
        targetNodeId: mutation.targetNodeId,
        labels: mutation.labels,
        valueKey: mutation.valueKey,
        newValue: mutation.newValue,
        tags: newTags);
  }

  GraphMutation withTag(dynamic newTag) {
    return new GraphMutation.fromMutationWithOverrides(this, tag: newTag);
  }

  bool get isEdgeMutation =>
      type == GraphMutationType.addEdge || type == GraphMutationType.removeEdge;

  bool get isNodeMutation =>
      type == GraphMutationType.addNode ||
      type == GraphMutationType.removeNode ||
      type == GraphMutationType.setValue;

  bool _validate() {
    switch (type) {
      case GraphMutationType.addNode:
      case GraphMutationType.removeNode:
        return nodeId != null &&
            edgeId == null &&
            valueKey == null &&
            newValue == null &&
            originNodeId == null &&
            targetNodeId == null &&
            labels == null;
      case GraphMutationType.addEdge:
      case GraphMutationType.removeEdge:
        return nodeId == null &&
            edgeId != null &&
            valueKey == null &&
            newValue == null &&
            originNodeId != null &&
            targetNodeId != null &&
            labels != null;
      case GraphMutationType.setValue:
        return nodeId != null &&
            edgeId == null &&
            valueKey != null &&
            originNodeId == null &&
            targetNodeId == null &&
            labels == null;
      default:
        // _validate() must be updated to include new [GraphMutationType].
        return false;
    }
  }

  @override
  String toString() {
    String extra;
    switch (type) {
      case GraphMutationType.addNode:
      case GraphMutationType.removeNode:
        extra = '$nodeId';
        break;
      case GraphMutationType.addEdge:
      case GraphMutationType.removeEdge:
        extra =
            '$edgeId: $originNodeId -> $targetNodeId [${labels.join(', ')}]';
        break;
      case GraphMutationType.setValue:
        extra = '$nodeId: $valueKey [';
        if (newValue != null) {
          extra += 'to ${newValue.length} bytes]';
        } else {
          extra += 'to no value]';
        }
        break;
    }
    if (tags != null && tags.isNotEmpty) {
      extra += ' tags: $tags';
    }
    return '$type: $extra';
  }

  dynamic toJson() {
    // TODO(ianloic): should timestamps be included in the JSON representation?
    switch (type) {
      case GraphMutationType.addNode:
        return {'type': 'addNode', 'node': nodeId.toString()};
      case GraphMutationType.removeNode:
        return {'type': 'removeNode', 'node': nodeId.toString()};
      case GraphMutationType.addEdge:
        return {
          'type': 'addEdge',
          'edge': edgeId.toString(),
          'origin': originNodeId.toString(),
          'target': targetNodeId.toString(),
          'labels': labels,
        };
      case GraphMutationType.removeEdge:
        return {
          'type': 'removeEdge',
          'edge': edgeId.toString(),
          'origin': originNodeId.toString(),
          'target': targetNodeId.toString(),
          'labels': labels,
        };
      case GraphMutationType.setValue:
        Map<String, String> json = {
          'type': 'setValue',
          'node': nodeId.toString(),
          'key': valueKey,
        };
        if (newValue != null) {
          json['value'] = BASE64.encode(newValue);
        }
        return json;
      default:
        throw new Exception('Unknown graph mutation type: $type');
    }
  }

  @override
  bool operator ==(other) =>
      other is GraphMutation &&
      other.type == type &&
      other.nodeId == nodeId &&
      other.edgeId == edgeId &&
      other.originNodeId == originNodeId &&
      other.targetNodeId == targetNodeId &&
      other.valueKey == valueKey &&
      const ListEquality<int>().equals(other.newValue, newValue) &&
      const ListEquality<String>().equals(other.labels, labels);

  @override
  int get hashCode => const ListEquality<int>().hash([
        type.hashCode,
        nodeId.hashCode,
        edgeId.hashCode,
        originNodeId.hashCode,
        targetNodeId.hashCode,
        valueKey.hashCode,
        newValue != null ? const ListEquality<int>().hash(newValue) : 0,
        labels != null ? const ListEquality<String>().hash(labels) : 0
      ]);
}

/// An exception class that is used when a set of [GraphMutations] could not
/// be applied to a [Graph] in a given state.
class FailedGraphMutation implements Exception {
  final List<GraphMutation> context;
  final GraphMutation failedMutation;
  final String _errorString;

  FailedGraphMutation(this.failedMutation, {String errorString, this.context})
      : _errorString = errorString ?? 'Failed to apply GraphMutations';

  @override
  String toString() {
    String error = '$_errorString\n'
        'failed mutation: $failedMutation';
    if (context != null) error += '\nwith context: $context';
    return error;
  }
}

/// Indicates that an 'addEdge' [GraphMutation] could not be applied because the
/// required origin node did not exist in the graph.
class MissingOriginNode extends FailedGraphMutation {
  MissingOriginNode(GraphMutation mutation, {List<GraphMutation> context})
      : super(mutation,
            context: context,
            errorString: 'new edge has missing origin node '
                '(${mutation.edgeId} ${mutation.originNodeId})');
}

/// Indicates that an 'addEdge' [GraphMutation] could not be applied because the
/// required target node did not exist in the graph.
class MissingTargetNode extends FailedGraphMutation {
  MissingTargetNode(GraphMutation mutation, {List<GraphMutation> context})
      : super(mutation,
            context: context,
            errorString: 'new edge has missing target node '
                '(${mutation.edgeId} ${mutation.targetNodeId})');
}

/// Indicates that a 'setValue' [GraphMutation] could not be applied because the
/// target node did not exist in the graph.
class MissingSetValueNode extends FailedGraphMutation {
  MissingSetValueNode(GraphMutation mutation, {List<GraphMutation> context})
      : super(mutation,
            context: context,
            errorString: 'cannot set value on missing node ${mutation.nodeId}');
}

/// Indicates that a [GraphMutation] could not be applied because the specified
/// origin node conflicts with the one in the pre-existing edge.
class ConflictingOriginNode extends FailedGraphMutation {
  final NodeId conflictingNodeId;
  ConflictingOriginNode(GraphMutation mutation, NodeId conflicting,
      {List<GraphMutation> context})
      : conflictingNodeId = conflicting,
        super(mutation,
            context: context,
            errorString: 'existing edge has conflicting origin node '
                '(${mutation.edgeId} ${mutation.originNodeId} $conflicting)');
}

/// Indicates that a [GraphMutation] could not be applied because the specified
/// target node conflicts with the one in the pre-existing edge.
class ConflictingTargetNode extends FailedGraphMutation {
  final NodeId conflictingNodeId;
  ConflictingTargetNode(GraphMutation mutation, NodeId conflicting,
      {List<GraphMutation> context})
      : conflictingNodeId = conflicting,
        super(mutation,
            context: context,
            errorString: 'existing edge has conflicting target node '
                '(${mutation.edgeId} ${mutation.targetNodeId} $conflicting)');
}

/// Indicates that a [GraphMutation] could not be applied because the specified
/// labels conflict with the those in the pre-existing edge.
class ConflictingEdgeLabels extends FailedGraphMutation {
  final List<String> conflictingLabels;
  ConflictingEdgeLabels(GraphMutation mutation, List<String> conflicting,
      {List<GraphMutation> context})
      : conflictingLabels = conflicting,
        super(mutation,
            context: context,
            errorString: 'existing edge has conflicting labels '
                '(${mutation.edgeId} ${mutation.labels} $conflicting)');
}
