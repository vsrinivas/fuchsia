// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

import 'graph.dart';

/// Delegates all methods to another [GraphState].  Subclasses must implement
/// the [state] getter.
abstract class DelegatingGraphState implements GraphState {
  GraphState get state;

  @override // GraphState
  bool containsNodeId(NodeId id) => state.containsNodeId(id);
  @override // GraphState
  bool containsEdgeId(EdgeId id) => state.containsEdgeId(id);
  @override // GraphState
  Iterable<NodeId> get nodeIds => state.nodeIds;
  @override // GraphState
  Iterable<EdgeId> get edgeIds => state.edgeIds;
  @override // GraphState
  Iterable<EdgeId> outEdgeIds(NodeId id) => state.outEdgeIds(id);
  @override // GraphState
  Iterable<EdgeId> inEdgeIds(NodeId id) => state.inEdgeIds(id);
  @override // GraphState
  Iterable<String> valueKeys(NodeId id) => state.valueKeys(id);
  @override // GraphState
  Uint8List getValue(NodeId id, String key) => state.getValue(id, key);
  @override // GraphState
  NodeId origin(EdgeId id) => state.origin(id);
  @override // GraphState
  NodeId target(EdgeId id) => state.target(id);
  @override // GraphState
  Iterable<String> labels(EdgeId id) => state.labels(id);
}
