// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:collection';

import 'mutation.dart';
import 'src/mutation_list.dart';

// An iterator for GraphMutation objects.
abstract class GraphMutationList extends IterableBase<GraphMutation> {
  GraphMutationList();

  factory GraphMutationList.from(Iterable<GraphMutation> mutations) {
    return new GraphMutationListImpl(mutations);
  }

  /// Returns a new GraphMutationList which coalesces all [GraphMutation]
  /// objects in such a way that ensures the current state of
  /// [this.graph] is coherent with the mutations given (ie, the observer will
  /// never see an addNode entry for a node that was subsequently deleted, and
  /// so on).
  ///
  /// If the mutations are already coalesced, returns [this].
  GraphMutationList get coalesced;

  /// Returns a list of GraphMutations where [mutation.tags.contains(tag)].
  GraphMutationList withTag(dynamic tag);

  /// Returns a list of GraphMutations where [!mutation.tags.contains(tag)].
  GraphMutationList withoutTag(dynamic tag);

  /// Returns the element at the given [index] or throws a [RangeError] if
  /// [index] is out of bounds.
  GraphMutation operator [](int index) => elementAt(index);
}
