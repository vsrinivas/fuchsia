// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert' show JSON;

import 'package:modular_services/ledger/ledger.mojom.dart';

/// A builder for queries for the SyncedStore.
///
/// For example, to create a query on edges with labels [A] and [B], starting
/// from [NodeRecord n] and that are not deleted, the code would be:
///     String query = (new JsonQueryBuilder()
///         ..addExpectedLabels([A, B])
///         ..setExpectedStart(n.nodeId)
///         ..setExpectedDeleted(deleted : false))
///         .buildQuery();
class JsonQueryBuilder {
  final Map<String, dynamic> _jsonQuery;

  JsonQueryBuilder() : _jsonQuery = {};

  /// Adds the given [labels] to the list of expected labels.
  void addExpectedLabels(final List<LabelUri> labels) {
    if (_jsonQuery["labels"] == null) {
      _jsonQuery["labels"] = [];
    }
    for (LabelUri label in labels) {
      _jsonQuery["labels"].add(label.uri);
    }
  }

  /// Sets the given [nodeId] as the expected end node.
  void setExpectedEnd(final NodeId nodeId) {
    _jsonQuery["end"] = nodeId.id;
  }

  /// Sets the given [nodeId] as the expected start node.
  void setExpectedStart(final NodeId nodeId) {
    _jsonQuery["start"] = nodeId.id;
  }

  /// Sets [deleted] as the expected value of the deleted property.
  void setExpectedDeleted({final bool deleted: true}) {
    _jsonQuery["deleted"] = deleted;
  }

  /// Creates and returns the Json string representation of this query.
  String buildQuery() => JSON.encode(_jsonQuery);
}
