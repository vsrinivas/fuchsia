// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import '../mutation.dart';
import '../mutation_list.dart';
import '../mutation_utils.dart';

class GraphMutationListImpl extends GraphMutationList {
  final List<GraphMutation> _mutations;

  GraphMutationListImpl _coalesced;

  GraphMutationListImpl(Iterable<GraphMutation> mutations,
      {bool isCoalesced: false})
      : _mutations = new List<GraphMutation>.from(mutations) {
    if (isCoalesced) _coalesced = this;
  }

  @override
  Iterator<GraphMutation> get iterator => _mutations.iterator;

  @override
  GraphMutationList get coalesced {
    if (_coalesced == null) {
      CoalescedMutations coalescedMutations = new CoalescedMutations();
      _mutations
          .forEach((GraphMutation m) => coalescedMutations.appendMutation(m));
      _coalesced = new GraphMutationListImpl(coalescedMutations.toList(),
          isCoalesced: true);
    }

    return _coalesced;
  }

  @override
  GraphMutationList withTag(dynamic tag) {
    return new GraphMutationListImpl(
        _mutations.where((GraphMutation m) => m.tags.contains(tag)));
  }

  @override
  GraphMutationList withoutTag(dynamic tag) {
    return new GraphMutationListImpl(
        _mutations.where((GraphMutation m) => !m.tags.contains(tag)));
  }
}
