// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/graph/mem_graph.dart';
import 'package:modular_core/entity/entity.dart';
import 'package:modular_core/entity/schema.dart' show Schema;
import 'package:parser/expression.dart';
import 'package:handler/session_pattern.dart';
import 'package:handler/module.dart' show SessionMatchUpdate;
import 'package:test/test.dart';

void main() {
  Graph graph;
  Node anchorNode;

  final Schema myTypeSchema = new Schema('myType', []);
  myTypeSchema.publish();

  final List<SessionPattern> patterns = [];

  setUp(() {
    graph = new MemGraph();
    graph.mutate((final GraphMutator gm) {
      anchorNode = gm.addNode();
    });

    // Whenever the Graph changes, just automatically update whatever patterns
    // we're tracking.
    graph.addObserver((final GraphEvent event) {
      final SessionMatchUpdate update =
          new SessionMatchUpdate.fromGraphMutations(graph, event.mutations);

      patterns.forEach(
          (final SessionPattern pattern) => pattern.updateMatches(update));
    });
  });

  test('Match Entity types', () {
    // Show that a PathExpr can match an Edge that points to an Entity of
    // the type specified in the PathExpr's label.
    final PathExpr nonMatchingExpr = new PathExpr.single(
        new Property([new Label.fromUri(Uri.parse('noMyType'))]));
    final SessionPattern nonMatching =
        new SessionPattern(nonMatchingExpr, [anchorNode]);

    final PathExpr matchingExpr = new PathExpr.single(
        new Property([new Label.fromUri(Uri.parse('myType'))]));
    final SessionPattern matching =
        new SessionPattern(matchingExpr, [anchorNode]);

    patterns.addAll([matching, nonMatching]);

    final Entity entity = new Entity(['myType']);
    Edge edge;
    graph.mutate((GraphMutator gm) {
      entity.save(gm);
      edge = gm.addEdge(anchorNode.id, ['label'], entity.node.id);
    });

    expect(nonMatching.isComplete, isFalse);
    expect(matching.isComplete, isTrue);
    expect(matching.matches.first.edgeList(0), equals([edge]));
  });
}
