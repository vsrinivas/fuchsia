// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:common/incrementer.dart';

/// This class helps to manage labels in a dot graph that represents a
/// session or recipe. It also keeps track of which nodes and edges
/// already exist, and thus helps to enforce uniqueness.
class LabelMap {
  final Map<String, String> labelId = <String, String>{};
  final Incrementer _incrementer = new Incrementer();

  final Set<String> _edge = new Set<String>();

  String getLabelId(final String label) {
    if (labelId.containsKey(label)) {
      return labelId[label];
    }
    final String id = 'label${_incrementer.next}';
    labelId[label] = id;
    return id;
  }

  // Records an edge if it doesn't exist yet. Returns whether it is in
  // fact new.
  bool isNewEdge(final String id0, final String id1) {
    final String key = '$id0:$id1';
    if (_edge.contains(key)) {
      return false;
    }
    _edge.add(key);
    return true;
  }
}
