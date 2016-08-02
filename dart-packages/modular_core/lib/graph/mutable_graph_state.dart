// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'graph.dart' show GraphState, GraphMutation;

abstract class MutableGraphState implements GraphState {
  /// Return true if the mutation was applied.  Subclasses may optionally
  /// return false if they determine that the mutation is redundant and did
  /// not need to be applied, and may optionally throw a [FailedGraphMutation]
  /// if the mutation is not compatible with the state, for example attempting
  /// to add an edge where:
  ///   - the origin and/or target nodes do not exist, or
  ///   - an edge with the same ID exists, but has a different origin or labels,
  ///   - etc.
  ///
  /// Note that subclasses are not required to detect invalid mutations; in such
  /// cases, callers must pre-validate any mutations that are to be applied.
  bool applyMutation(GraphMutation mutation);
}
