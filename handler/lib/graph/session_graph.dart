// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/graph/query/query.dart' show GraphQuery;
import 'package:modular_core/uuid.dart' show Uuid;

import 'session_graph_link.dart' show SessionGraphLink;

export 'package:modular_core/graph/graph.dart';
export 'package:modular_core/uuid.dart' show Uuid;
export 'session_graph_link.dart' show SessionGraphLink;

/// [SessionGraph] wraps an existing [Graph] (typically one that is synced to
/// the Ledger, but this is not required).  Most of the [Nodes]/[Edges] in the
/// wrapped [Graph] (the 'baseGraph') are exposed, but some are filtered out,
/// and others are added.
///
/// [SessionGraph] instances are not instantiated directly, but instead obtained
/// from a [SessionGraphStore].
abstract class SessionGraph extends Graph {
  /// Return descriptions of all sessions that this [SessionGraph] links to.
  Iterable<SessionGraphLink> get links;

  /// Return the root [Node] of the [SessionGraph].
  Node get root;

  /// Return the metadata [Node] of the [SessionGraph].
  Node get metadataNode;

  /// Link to another session, and return a [SessionGraphLink], which is a
  /// descriptive token that can be used to remove the link by passing it to
  /// [SessionGraph.removeSessionLink].
  ///
  /// [sessionId] identifies the [SessionGraph] to link to, and is used to look
  /// it up from the [SessionGraphStore] passed to the constructor.  Only those
  /// nodes/edges of the linked graph that match [query] are exposed; the set of
  /// matches dynamically updates in response to changes in the linked graph.
  /// If [linkOrigin] is not null, an [Edge] is generated from this origin to
  /// the root [Node] of each [query] match; this edge is labeled by [labels].
  ///
  /// Returns null if the link could not be created for some reason (for
  /// example, if the link already exists).
  SessionGraphLink addSessionLink(Uuid sessionId,
      {GraphQuery query, Node linkOrigin, Iterable<String> labels});

  /// Remove a link to another session that was previously added by
  /// [SessionGraph.addSessionLink].  No-op if the link does not exist.
  void removeSessionLink(SessionGraphLink spec);

  @override
  dynamic toJson() {
    Map<String, dynamic> json = super.toJson();
    json['rootNode'] = root?.id?.toString();
    json['metadataNode'] = metadataNode?.id?.toString();
    // TODO(ianloic): include [links] too?
    return json;
  }
}
