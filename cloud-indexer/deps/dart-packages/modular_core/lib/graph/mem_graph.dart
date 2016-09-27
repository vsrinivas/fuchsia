// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'id.dart';
import 'mem_graph_state.dart';
import 'simple_graph.dart';

class MemGraph extends SimpleGraph {
  MemGraph(
      {String prefix: null,
      NodeIdGenerator nodeIdGen,
      EdgeIdGenerator edgeIdGen})
      : super(new MemGraphState(),
            nodeIdGen ?? new PrefixNodeIdGenerator(prefix),
            edgeIdGen ?? new PrefixEdgeIdGenerator(prefix));
}
