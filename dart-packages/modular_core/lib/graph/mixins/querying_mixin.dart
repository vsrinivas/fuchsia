// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import '../graph.dart';
import '../query/query.dart';
import '../query/query_match_set.dart';

abstract class GraphQueryingMixin {
  /// This mixin is designed to be mixed into a [Graph] implementation. In order
  /// to have access to what in the future will be [this], we need to have
  /// the implementation override this method and return [this].
  Graph get mixinGraph;

  GraphQueryMatchSet query(final GraphQuery query) {
    return new GraphQueryMatchSetImpl(mixinGraph, query);
  }
}
