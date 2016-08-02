// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';

import 'package:modular_core/log.dart';
import 'package:modular_core/util/base64_converter.dart';
import 'package:modular_services/ledger/ledger.mojom.dart';

/// The base class for a row in a session.
///
/// Classes extending [SessionRow] permit the serialization of a given type of
/// row (e.g. node row) from their ids and records (e.g. [NodeId] and
/// [NodeRecord]) to their String representation, as it should appear in the
/// [SyncedStore], and vice versa.
/// Instances of the subclasses can be used to load, write and query data from
/// the [SyncedStore].
abstract class SessionRow {
  static final Logger _log = log("SessionRow");

  static const String _rowPrefix = "session_";

  final SessionId sessionId;

  SessionRow(this.sessionId) {
    assert(sessionId != null);
  }

  /// Returns a new [NodeRow]. If [nodeId] and/or [nodeRecord] are not provided
  /// the row should only be used for quering stored nodes of the session.
  static NodeRow nodeRow(SessionId sessionId,
      [NodeId nodeId, NodeRecord nodeRecord]) {
    return new NodeRow(sessionId, nodeId, nodeRecord);
  }

  /// Returns a new [EdgeRow]. If [edgeId] and/or [edgeRecord] are not provided
  /// the row should only be used for quering stored edges of the session.
  static EdgeRow edgeRow(SessionId sessionId,
      [EdgeId edgeId, EdgeRecord edgeRecord]) {
    return new EdgeRow(sessionId, edgeId, edgeRecord);
  }

  static SessionRow representationRow(SessionId sessionId,
      [NodeId nodeId, LabelUri label, Representation representation]) {
    return new RepresentationRow(sessionId, nodeId, label, representation);
  }

  /// Returns a new [SessionRow] holding information only on the [sessionId]
  /// prefix. The result can only be used for quering stored session's
  /// data.
  static SessionRow sessionPrefix(SessionId sessionId) {
    return new _SessionRowImpl(sessionId);
  }

  /// Returns a new [SessionRow] from the key-value strings as they are
  /// represented in the [SyncedStore].
  static SessionRow fromSyncedStore(String key, String value) {
    final List<String> keySubs = key.split("/");
    assert(keySubs.length >= 2 && keySubs.length <= 3);

    final SessionId sessionId = new SessionId()
      ..id = Base64.decodeToString(keySubs[0].substring(_rowPrefix.length));

    if (keySubs[1].startsWith(NodeRow.rowPrefix)) {
      return new NodeRow.fromSyncedStore(sessionId, keySubs[1], value);
    } else if (keySubs[1].startsWith(EdgeRow.rowPrefix)) {
      return new EdgeRow.fromSyncedStore(sessionId, keySubs[1], value);
    } else if (keySubs[1].startsWith(RepresentationRow.rowPrefix)) {
      return new RepresentationRow.fromSyncedStore(
          sessionId, keySubs[1], keySubs[2], value);
    }
    // Unreachable statement.
    _log.warning("Failed to parse row with key: $key, value: $value");
    assert(false);
    return null;
  }

  /// The key of the row as it will be represented in the synced store.
  String printableKey();

  /// The value of the row as it will be represented in the synced store.
  String printableValue();

  /// Adds the record of this [SessionRow] in the given graph. Returns true if
  /// the record has successfully been added; false otherwise.
  bool addInGraph(SessionGraph graph) => false;

  /// Returns the [GraphUpdate] representation of this row. This method should
  /// not be called on rows used for querying the synced store.
  GraphUpdate toGraphUpdate() {
    assert(false);
    return null;
  }

  @override
  bool operator ==(Object other) {
    return other is SessionRow && printableKey() == other.printableKey();
  }

  @override
  int get hashCode => printableKey().hashCode;

  String _sessionIdPrefix() {
    return _rowPrefix + Base64.encodeString(sessionId.id) + "/";
  }
}

/// A simple implementation of [SessionRow]. To be used for querying all
/// session-related data stored in the Ledger.
class _SessionRowImpl extends SessionRow {
  _SessionRowImpl(SessionId sessionId) : super(sessionId);

  @override
  String printableKey() => _sessionIdPrefix();

  @override
  String printableValue() => "";
}

/// A [NodeRow] is serialized as:
///     session_<base64-sessionId>/node_<base64-nodeId>
///     : <node contents in json>
class NodeRow extends SessionRow {
  static const String rowPrefix = "node_";

  final NodeId nodeId;
  final NodeRecord nodeRecord;

  NodeRow(SessionId sessionId, this.nodeId, this.nodeRecord) : super(sessionId);

  NodeRow.fromSyncedStore(SessionId sessionId, String nodeIdKey, String value)
      : nodeId = new NodeId()
          ..id = Base64.decodeToString(nodeIdKey.substring(rowPrefix.length)),
        nodeRecord = value != null ? _jsonToNodeRecord(value) : null,
        super(sessionId) {
    if (nodeRecord != null) {
      nodeRecord.nodeId = nodeId;
    }
  }

  @override
  String printableKey() {
    return _sessionIdPrefix() +
        rowPrefix +
        (nodeId != null ? Base64.encodeString(nodeId.id) : "");
  }

  @override
  String printableValue() => _nodeRecordToJson(nodeRecord);

  @override
  bool addInGraph(SessionGraph graph) {
    if (nodeRecord != null) {
      graph.nodes.add(nodeRecord);
      return true;
    }
    return false;
  }

  @override
  GraphUpdate toGraphUpdate() {
    assert(nodeRecord != null);

    if (nodeRecord.deleted) {
      return new GraphUpdate()..nodeRemoved = nodeId;
    }
    return new GraphUpdate()..nodeAdded = nodeRecord;
  }

  String _nodeRecordToJson(NodeRecord nodeRecord) {
    if (nodeRecord == null) {
      return "";
    }
    // |nodeId| should not be persisted.
    return JSON.encode({
      "creationDevice": nodeRecord.creationDevice.id,
      "creationUser": nodeRecord.creationUser.id,
      "deleted": nodeRecord.deleted,
    });
  }

  // From Json string to NodeRecord
  static NodeRecord _jsonToNodeRecord(String jsonString) {
    Map<String, dynamic> map = JSON.decode(jsonString);
    return new NodeRecord()
      ..creationDevice = (new DeviceId()..id = map["creationDevice"])
      ..creationUser = (new UserId()..id = map["creationUser"])
      ..deleted = map["deleted"];
  }
}

/// A [RepresentationRow] is serialized as:
///     session_<base64-sessionId>/representation_<base64-nodeId>/<base64-label>
///     : {"label": <label>, "timestamp": <timestamp>, "value": <value>}
class RepresentationRow extends SessionRow {
  static const String rowPrefix = "representation_";

  final NodeId nodeId;
  final LabelUri label;
  final Representation representation;

  RepresentationRow(
      SessionId sessionId, this.nodeId, this.label, this.representation)
      : super(sessionId);

  factory RepresentationRow.fromSyncedStore(
      SessionId sessionId, String nodeIdKey, String label, String value) {
    final NodeId nodeId = new NodeId()
      ..id = Base64.decodeToString(nodeIdKey.substring(rowPrefix.length));
    if (value == null) {
      return new RepresentationRow(sessionId, nodeId, null, null);
    }
    final Map<String, dynamic> map = JSON.decode(value);
    final Representation representation = new Representation()
      ..nodeId = nodeId
      ..label = (new LabelUri()..uri = map["label"])
      ..timestamp = map["timestamp"]
      ..value = map["value"];
    return new RepresentationRow(
        sessionId, nodeId, representation.label, representation);
  }

  @override
  String printableKey() {
    return _sessionIdPrefix() +
        rowPrefix +
        (nodeId != null ? Base64.encodeString(nodeId.id) : "") +
        (nodeId != null && label.uri != null
            ? '/' + Base64.encodeString(label.uri)
            : "");
  }

  @override
  String printableValue() {
    if (label == null) {
      return "";
    }
    return JSON.encode({
      "label": representation.label.uri,
      "timestamp": representation.timestamp,
      "value": representation.value
    });
  }

  @override
  bool addInGraph(SessionGraph graph) {
    if (nodeId == null || label == null) {
      // This is a query row, cannot add it in a graph.
      return false;
    }
    // Find the node, or create a new one if it doesn't exist.
    NodeRecord target = graph.nodes.firstWhere(
        (NodeRecord node) => node.nodeId.id == nodeId.id,
        orElse: () => null);
    if (target == null) {
      // The target node is not in the graph.
      return false;
    }
    target.representationValues ??= [];
    target.representationValues.add(representation);
    return true;
  }

  @override
  GraphUpdate toGraphUpdate() {
    assert(nodeId != null && label != null);

    return new GraphUpdate()
      ..valueUpdated = (new Representation()
        ..nodeId = nodeId
        ..label = label
        ..timestamp = representation.timestamp
        ..value = representation.value);
  }
}

/// An [EdgeRow] is serialized as:
///     session_<base64-sessionId>/edge_<base64-edgeId>
///     : <edge contents in json>
class EdgeRow extends SessionRow {
  static const String rowPrefix = "edge_";

  final EdgeId edgeId;
  final EdgeRecord edgeRecord;

  EdgeRow(SessionId sessionId, this.edgeId, this.edgeRecord) : super(sessionId);

  EdgeRow.fromSyncedStore(SessionId sessionId, String edgeIdKey, String value)
      : edgeId = new EdgeId()
          ..id = Base64.decodeToString(edgeIdKey.substring(rowPrefix.length)),
        edgeRecord = value != null ? _jsonToEdgeRecord(value) : null,
        super(sessionId) {
    if (edgeRecord != null) {
      edgeRecord.edgeId = edgeId;
    }
  }

  @override
  String printableKey() {
    return _sessionIdPrefix() +
        rowPrefix +
        (edgeId != null ? Base64.encodeString(edgeId.id) : "");
  }

  @override
  String printableValue() {
    if (edgeRecord == null) {
      return "";
    }
    // |edgeId| should not be persisted.
    return JSON.encode({
      "start": edgeRecord.start.id,
      "end": edgeRecord.end.id,
      "labels": edgeRecord.labels.map((LabelUri label) => label.uri).toList(),
      "creationDevice": edgeRecord.creationDevice.id,
      "creationUser": edgeRecord.creationUser.id,
      "deleted": edgeRecord.deleted,
    });
  }

  @override
  bool addInGraph(SessionGraph graph) {
    if (edgeRecord != null) {
      graph.edges.add(edgeRecord);
      return true;
    }
    return false;
  }

  @override
  GraphUpdate toGraphUpdate() {
    assert(edgeRecord != null);

    if (edgeRecord.deleted) {
      return new GraphUpdate()..edgeRemoved = edgeId;
    }
    return new GraphUpdate()..edgeAdded = edgeRecord;
  }

  static List<LabelUri> _jsonToURIList(List<String> list) {
    return list.map((String e) => new LabelUri()..uri = e).toList();
  }

  // From Json string to EdgeRecord
  static EdgeRecord _jsonToEdgeRecord(String jsonString) {
    Map<String, dynamic> map = JSON.decode(jsonString);
    return new EdgeRecord()
      ..start = (new NodeId()..id = map["start"])
      ..end = (new NodeId()..id = map["end"])
      ..labels = _jsonToURIList(map["labels"])
      ..creationDevice = (new DeviceId()..id = map["creationDevice"])
      ..creationUser = (new UserId()..id = map["creationUser"])
      ..deleted = map["deleted"];
  }
}
