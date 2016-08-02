// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:modular_core/uuid.dart';
import 'package:modular_core/graph/graph.dart';

import '../../constants.dart';
import '../graph_store.dart';

class FakeLedgerGraphStore extends InMemoryGraphStore {
  FakeLedgerGraphStore({GraphStorePrefixGenerator prefixGenerator})
      : super(prefixGenerator: prefixGenerator);

  @override
  Future<Graph> createGraph(Uuid sessionId) {
    final future = super.createGraph(sessionId);
    future.then((Graph graph) {
      graph.mutate((GraphMutator mutator) {
        final NodeId rootId = new NodeId.fromString(
            '${Constants.sessionGraphLabelPrefix}rootNode:$sessionId');
        mutator.apply(new GraphMutation.addNode(rootId));

        Node metadataNode = mutator.addNode();
        mutator.setValue(metadataNode.id, Constants.sessionGraphIdLabel,
            sessionId.toUint8List());
        mutator.addEdge(rootId, [Constants.metadataLabel], metadataNode.id);
      });
    });
    return future;
  }
}
