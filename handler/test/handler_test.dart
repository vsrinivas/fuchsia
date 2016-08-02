// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

import 'package:common/incrementer.dart';
import 'package:handler/graph/graph_store.dart';
import 'package:handler/handler.dart';
import 'package:handler/manifest_matcher.dart';
import 'package:handler/module_instance.dart';
import 'package:handler/module_runner.dart';
import 'package:handler/session.dart';
import 'package:handler/session_pattern.dart';
import 'package:modular/builtin_types.dart';
import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/graph/test_utils/mutation_helper.dart';
import 'package:parser/manifest.dart';
import 'package:parser/parser.dart';
import 'package:parser/recipe.dart';
import 'package:test/test.dart';

import '../test_util/recording_module_runner.dart';

int _countNotNull(int prev, dynamic item) => prev + (item != null ? 1 : 0);

class DummyModuleRunner implements ModuleRunner {
  @override
  void start(final ModuleInstance instance) {}

  @override
  void update() {}

  @override
  void stop() {}
}

/// ManifestMatcher that synthetizes a manifest match for all steps provided.
class DummyManifestMatcher implements ManifestMatcher {
  @override
  List<Manifest> get manifests => [];

  @override
  Manifest selectManifest(final Step step) => new Manifest.fromStep(step);

  @override
  void addOrUpdateManifest(final Manifest updatedManifest) {}
}

final ModuleRunnerFactory _dummyRunnerFactory = () => new DummyModuleRunner();

void main() {
  final List<String> l0 = ["http://tq.io/p0"];
  final List<String> l1 = ["http://tq.io/p1"];
  final List<String> l2 = ["http://tq.io/p2"];
  final List<String> l3 = ["http://tq.io/p3"];
  final List<String> l4 = ["http://tq.io/p4"];
  final List<String> l5 = ["http://tq.io/p5"];

  group('Handler', () {
    test('Init and update single inputs', () async {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input:
            - p1
            - p2 -> p3

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
      ''';
      final Recipe recipe = parseRecipe(recipeText);
      final Handler handler = new Handler.fromManifestMatcher(
          new DummyManifestMatcher(),
          runnerFactory: _dummyRunnerFactory);
      final Session session = await handler.createSession(recipe);
      final MutationHelper graph = new MutationHelper(session.graph);

      final Node n1 = graph.addNode(id: 'n1');
      final Node n2 = graph.addNode(id: 'n2');
      final Node n3 = graph.addNode(id: 'n3');
      final Node n4 = graph.addNode(id: 'n4');
      final Edge e1 = graph.addEdge(session.root, l1, n1, id: 'e1');
      final Edge e2 = graph.addEdge(session.root, l2, n2, id: 'e2');
      final Edge e3 = graph.addEdge(e2.target, l3, n3, id: 'e3');
      final Edge e4 = graph.addEdge(session.root, l2, n4, id: 'e4');

      session.start();

      expect(session.modules.length, equals(1));

      expect(session.modules[0].instances.length, equals(1));
      expect(session.modules[0].instances[0].inputMatches.length, equals(2));
      expect(session.modules[0].instances[0].inputMatches[0].target(0, 0),
          equals(e1.target));
      expect(session.modules[0].instances[0].inputMatches[1].target(0, 0),
          equals(e3.target));

      expect(session.modules[0].inputs[0].matches.length, equals(1));
      expect(session.modules[0].inputs[0].matches[0].isComplete, isTrue);

      expect(session.modules[0].inputs[0].matches[0].target(0, 0),
          equals(e1.target));

      expect(session.modules[0].inputs[1].matches.length, equals(2));
      expect(session.modules[0].inputs[1].matches[0].isComplete, isTrue);
      expect(session.modules[0].inputs[1].matches[1].isComplete, isFalse);

      expect(session.modules[0].inputs[1].matches[0].target(0, 0),
          equals(e2.target));
      expect(session.modules[0].inputs[1].matches[0].target(1, 0),
          equals(e3.target));
      expect(session.modules[0].inputs[1].matches[1].target(0, 0),
          equals(e4.target));

      final Node n5 = graph.addNode(id: 'n5');
      final Edge e5 = graph.addEdge(e4.target, l3, n5, id: 'e5');

      expect(session.modules[0].instances.length, equals(1));

      expect(session.modules[0].inputs[0].matches.length, equals(1));
      expect(session.modules[0].inputs[0].matches[0].isComplete, isTrue);

      expect(session.modules[0].inputs[1].matches.length, equals(2));
      expect(session.modules[0].inputs[1].matches[0].isComplete, isTrue);
      expect(session.modules[0].inputs[1].matches[1].isComplete, isTrue);

      expect(session.modules[0].inputs[1].matches[1].target(0, 0),
          equals(e4.target));
      expect(session.modules[0].inputs[1].matches[1].target(1, 0),
          equals(e5.target));

      final Node n6 = graph.addNode(id: 'n6');
      final Node n7 = graph.addNode(id: 'n7');
      final Edge e6 = graph.addEdge(session.root, l0, n6, id: 'e6');
      /* final Edge e7 = */ graph.addEdge(e6.target, l1, n7, id: 'e7');

      expect(session.modules[0].instances.length, equals(1));

      expect(session.modules[0].inputs[0].matches.length, equals(1));
      expect(session.modules[0].inputs[0].matches[0].isComplete, isTrue);

      expect(session.modules[0].inputs[1].matches.length, equals(2));
      expect(session.modules[0].inputs[1].matches[0].isComplete, isTrue);
      expect(session.modules[0].inputs[1].matches[1].isComplete, isTrue);
    });

    test('PathExpr matching', () async {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input:
            - p0 -> p1 -> p5
            - p0 -> p2 -> p3 -> p4 -> p5

        use:
         - v1: http://tq.io/v1
         - p0: http://tq.io/p0
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
         - p4: http://tq.io/p4
         - p5: http://tq.io/p5
      ''';
      final Recipe recipe = parseRecipe(recipeText);
      final Handler handler = new Handler.fromManifestMatcher(
          new DummyManifestMatcher(),
          runnerFactory: _dummyRunnerFactory);
      final Session session = await handler.createSession(recipe);
      final MutationHelper graph = new MutationHelper(session.graph);

      // HACK: Creating a dummy edge(with l0 label) at the start so that we can
      // have a test for loops. We need this because in code
      // recipe_runner:update, the way we get access to disconnected parts is by
      // accessing the roots of the disconnected graphs, and root is assumed to
      // not have any inEdges.
      final Node n0 = graph.addNode(id: 'n0');
      final Edge e0 = graph.addEdge(session.root, l0, n0);

      // p0->p1->p5
      final Node n1 = graph.addNode(id: 'n1');
      final Node n5 = graph.addNode(id: 'n5');
      final Edge e11 = graph.addEdge(e0.target, l1, n1, id: 'e11');
      final Edge e51 = graph.addEdge(e11.target, l5, n5, id: 'e51');
      // p0->p2->p3->p4->p5
      final Node n2 = graph.addNode(id: 'n2');
      final Node n3 = graph.addNode(id: 'n3');
      final Edge e2 = graph.addEdge(e0.target, l2, n2, id: 'e2');
      final Edge e3 = graph.addEdge(e2.target, l3, n3, id: 'e3');
      final Edge e4 = graph.addEdge(e3.target, l4, e51.origin, id: 'e4');

      session.start();

      expect(session.modules.length, equals(1));

      // verb: v1
      expect(session.modules[0].inputs.length, equals(2));
      expect(session.modules[0].instances.length, equals(1));
      expect(session.modules[0].instances[0].anchorMatches.length, equals(2));
      expect(session.modules[0].instances[0].anchorMatches[0].target(0, 0),
          equals(n0));

      // verb: v1, input: p0->p1->p5
      expect(session.modules[0].inputs[0].matches.length, equals(1));
      expect(session.modules[0].inputs[0].matches[0].isComplete, isTrue);
      expect(session.modules[0].inputs[0].matches[0].target(1, 0),
          equals(e11.target));
      expect(session.modules[0].inputs[0].matches[0].target(2, 0),
          equals(e51.target));

      // verb: v1, input: p0 -> p2 -> p3 -> p4 -> p5
      expect(session.modules[0].inputs[1].matches.length, equals(1));
      expect(session.modules[0].inputs[1].matches[0].isComplete, isTrue);
      expect(session.modules[0].inputs[1].matches[0].target(1, 0),
          equals(e2.target));
      expect(session.modules[0].inputs[1].matches[0].target(2, 0),
          equals(e3.target));
      expect(session.modules[0].inputs[1].matches[0].target(3, 0),
          equals(e4.target));
      expect(session.modules[0].inputs[1].matches[0].target(4, 0),
          equals(e51.target));

      // Add another p5 edge from p1 end.
      final Edge e52 = graph.addEdge(e11.target, l5, null);

      // verb: v1, input: p1->p5
      expect(session.modules[0].inputs[0].matches.length, equals(2));
      expect(session.modules[0].inputs[0].matches[0].isComplete, isTrue);
      expect(session.modules[0].inputs[0].matches[1].isComplete, isTrue);
      expect(session.modules[0].inputs[0].matches[1].target(1, 0),
          equals(e11.target));
      expect(session.modules[0].inputs[0].matches[1].target(2, 0),
          equals(e52.target));

      // verb: v1, input: p2 -> p3 -> p4 -> p5
      expect(session.modules[0].inputs[1].matches.length, equals(2));
      expect(session.modules[0].inputs[1].matches[0].isComplete, isTrue);
      expect(session.modules[0].inputs[1].matches[1].isComplete, isTrue);
      expect(session.modules[0].inputs[1].matches[1].target(1, 0),
          equals(e2.target));
      expect(session.modules[0].inputs[1].matches[1].target(2, 0),
          equals(e3.target));
      expect(session.modules[0].inputs[1].matches[1].target(3, 0),
          equals(e4.target));
      expect(session.modules[0].inputs[1].matches[1].target(4, 0),
          equals(e52.target));

      // Test cycle: Add another p5 edge from p1 end to p1 origin.
      final Edge e53 = graph.addEdge(e11.target, l5, e11.origin);

      // verb: v1, input: p1->p5
      expect(session.modules[0].inputs[0].matches.length, equals(3));
      expect(session.modules[0].inputs[0].matches[0].isComplete, isTrue);
      expect(session.modules[0].inputs[0].matches[1].isComplete, isTrue);
      expect(session.modules[0].inputs[0].matches[2].isComplete, isTrue);
      expect(session.modules[0].inputs[0].matches[2].target(1, 0),
          equals(e11.target));
      expect(session.modules[0].inputs[0].matches[2].target(2, 0),
          equals(e53.target));

      // verb: v1, input: p2 -> p3 -> p4 -> p5
      expect(session.modules[0].inputs[1].matches.length, equals(3));
      expect(session.modules[0].inputs[1].matches[0].isComplete, isTrue);
      expect(session.modules[0].inputs[1].matches[1].isComplete, isTrue);
      expect(session.modules[0].inputs[1].matches[2].isComplete, isTrue);
      expect(session.modules[0].inputs[1].matches[2].target(1, 0),
          equals(e2.target));
      expect(session.modules[0].inputs[1].matches[2].target(2, 0),
          equals(e3.target));
      expect(session.modules[0].inputs[1].matches[2].target(3, 0),
          equals(e4.target));
      expect(session.modules[0].inputs[1].matches[2].target(4, 0),
          equals(e53.target));
    });

    test('PathExpr matching', () async {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input:
            - p3 -> p4 -> p2

        use:
         - v1: http://tq.io/v1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
         - p4: http://tq.io/p4
      ''';
      final Recipe recipe = parseRecipe(recipeText);
      final Handler handler = new Handler.fromManifestMatcher(
          new DummyManifestMatcher(),
          runnerFactory: _dummyRunnerFactory);
      final Session session = await handler.createSession(recipe);
      final MutationHelper graph = new MutationHelper(session.graph);

      // HACK: Creating a dummy edge(with l0 label) at the start so that we can
      // have a test for loops. We need this because in code
      // recipe_runner:update, the way we get access to disconnected parts is by
      // accessing the roots of the disconnected graphs, and root is assumed to
      // not have any inEdges.
      final Node n0 = graph.addNode(id: 'n0');
      final Edge e0 = graph.addEdge(session.root, l0, n0);

      // p0->p1->p2
      final Node n1 = graph.addNode(id: 'n1');
      final Edge e1 = graph.addEdge(e0.target, l1, n1, id: 'e1');
      final Node n2 = graph.addNode(id: 'n2');
      graph.addEdge(e1.target, l2, n2, id: 'e2');

      // p3
      // Note that edge for p3 should start from the session root as the recipe
      // path expressions are matched from session root.
      final Node n3 = graph.addNode(id: 'n3');
      final Edge e3 = graph.addEdge(session.root, l3, n3, id: 'e3');

      session.start();

      expect(session.modules.length, equals(1));

      // verb: v1
      expect(session.modules[0].inputs.length, equals(1));
      expect(session.modules[0].instances.length, equals(1));
      expect(session.modules[0].instances[0], isNull);

      // Add edge p4 between p3.target to p1.target. This edge should match the
      // path expression for step in recipe and should instantiate the module.
      graph.addEdge(e3.target, l4, e1.target, id: 'e4');

      expect(session.modules[0].instances.length, equals(1));
      expect(session.modules[0].instances[0], isNotNull);
    });

    test('Init and update repeated inputs', () async {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input:
            - p1+
            - p2+ -> p3

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
      ''';
      final Recipe recipe = parseRecipe(recipeText);
      final Handler handler = new Handler.fromManifestMatcher(
          new DummyManifestMatcher(),
          runnerFactory: _dummyRunnerFactory);
      final Session session = await handler.createSession(recipe);
      final MutationHelper graph = new MutationHelper(session.graph);

      final Edge e1 = graph.addEdge(session.root, l1, null);
      final Edge e2 = graph.addEdge(session.root, l2, null);
      final Edge e3 = graph.addEdge(e2.target, l3, null);
      final Edge e4 = graph.addEdge(session.root, l2, null);

      session.start();

      expect(session.modules.length, equals(1));

      // verb: v1
      expect(session.modules[0].inputs.length, equals(2));
      expect(session.modules[0].instances.length, equals(1));

      // verb: v1, input: p1
      expect(session.modules[0].inputs[0].matches.length, equals(1));
      expect(session.modules[0].inputs[0].matches[0].target(0, 0),
          equals(e1.target));

      // verb: v1, input: p2 -> p3
      expect(session.modules[0].inputs[1].matches.length, equals(1));
      expect(session.modules[0].inputs[1].matches[0].target(0, 0),
          equals(e2.target));
      expect(session.modules[0].inputs[1].matches[0].target(1, 0),
          equals(e3.target));

      final Edge e5 = graph.addEdge(e4.target, l3, null);

      // verb: v1
      expect(session.modules[0].instances.length, equals(1));

      // verb: v1, input: p2 -> p3
      expect(session.modules[0].inputs[1].matches.length, equals(1));
      expect(session.modules[0].inputs[1].matches[0].targetList(0).length,
          equals(2));
      expect(session.modules[0].inputs[1].matches[0].targetList(1).length,
          equals(2));
      expect(session.modules[0].inputs[1].matches[0].target(0, 1),
          equals(e4.target));
      expect(session.modules[0].inputs[1].matches[0].target(1, 1),
          equals(e5.target));

      // Not a match.
      final Edge e6 = graph.addEdge(session.root, l0, null);
      /* final Edge e7 = */ graph.addEdge(e6.target, l1, null);

      // verb: v1
      expect(session.modules[0].instances.length, equals(1));

      // verb: v1, input: p1
      expect(session.modules[0].inputs[0].matches.length, equals(1));
      expect(session.modules[0].inputs[0].matches[0].targetList(0).length,
          equals(1));
    });

    test('Input paths with common roots, multiple root nodes', () async {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           scope: p1
           input:
            - p1 -> p2
            - p1 -> p3

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
      ''';
      final Recipe recipe = parseRecipe(recipeText);
      final Handler handler = new Handler.fromManifestMatcher(
          new DummyManifestMatcher(),
          runnerFactory: _dummyRunnerFactory);
      final Session session = await handler.createSession(recipe);
      final MutationHelper graph = new MutationHelper(session.graph);

      final Edge e1 = graph.addEdge(session.root, l1, null);
      /* final Edge e2 = */ graph.addEdge(e1.target, l2, null);
      /* final Edge e3 = */ graph.addEdge(e1.target, l3, null);

      final Edge e4 = graph.addEdge(session.root, l1, null);
      /* final Edge e5 = */ graph.addEdge(e4.target, l2, null);
      /* final Edge e6 = */ graph.addEdge(e4.target, l3, null);

      final Edge e7 = graph.addEdge(session.root, l1, null);
      /* final Edge e8 = */ graph.addEdge(e7.target, l2, null);
      /* final Edge e9 = */ graph.addEdge(e7.target, l3, null);

      session.start();

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(2));
      expect(session.modules[0].instances.length, equals(3));
      expect(session.modules[0].instances.fold(0, _countNotNull), equals(3));
    });

    test('Input paths with common roots, multiple root nodes, no scope',
        () async {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input:
            - p1 -> p2
            - p1 -> p3

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
      ''';
      final Recipe recipe = parseRecipe(recipeText);
      final Handler handler = new Handler.fromManifestMatcher(
          new DummyManifestMatcher(),
          runnerFactory: _dummyRunnerFactory);
      final Session session = await handler.createSession(recipe);
      final MutationHelper graph = new MutationHelper(session.graph);

      final Edge e1 = graph.addEdge(session.root, l1, null);
      /* final Edge e2 = */ graph.addEdge(e1.target, l2, null);
      /* final Edge e3 = */ graph.addEdge(e1.target, l3, null);

      final Edge e4 = graph.addEdge(session.root, l1, null);
      /* final Edge e5 = */ graph.addEdge(e4.target, l2, null);
      /* final Edge e6 = */ graph.addEdge(e4.target, l3, null);

      final Edge e7 = graph.addEdge(session.root, l1, null);
      /* final Edge e8 = */ graph.addEdge(e7.target, l2, null);
      /* final Edge e9 = */ graph.addEdge(e7.target, l3, null);

      session.start();

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(2));
      expect(session.modules[0].instances.length, equals(1));
      expect(session.modules[0].instances.fold(0, _countNotNull), equals(1));
    });

    test('Input paths with repeated/singular roots, multiple root nodes',
        () async {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           scope: p1
           input:
            - p1+ -> p2
            - p1 -> p3

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
      ''';
      final Recipe recipe = parseRecipe(recipeText);
      final Handler handler = new Handler.fromManifestMatcher(
          new DummyManifestMatcher(),
          runnerFactory: _dummyRunnerFactory);
      final Session session = await handler.createSession(recipe);
      final MutationHelper graph = new MutationHelper(session.graph);

      final Edge e1 = graph.addEdge(session.root, l1, null);
      /* final Edge e2 = */ graph.addEdge(e1.target, l2, null);
      /* final Edge e3 = */ graph.addEdge(e1.target, l3, null);

      final Edge e4 = graph.addEdge(session.root, l1, null);
      /* final Edge e5 = */ graph.addEdge(e4.target, l2, null);
      /* final Edge e6 = */ graph.addEdge(e4.target, l3, null);

      final Edge e7 = graph.addEdge(session.root, l1, null);
      /* final Edge e8 = */ graph.addEdge(e7.target, l2, null);
      /* final Edge e9 = */ graph.addEdge(e7.target, l3, null);

      session.start();

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(2));
      expect(session.modules[0].instances.length, equals(3));
      expect(session.modules[0].instances.fold(0, _countNotNull), equals(3));
    });

    test(
        'Input paths with repeated/singular roots, multiple root nodes, no scope',
        () async {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input:
            - p1+ -> p2
            - p1 -> p3

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
      ''';
      final Recipe recipe = parseRecipe(recipeText);
      final Handler handler = new Handler.fromManifestMatcher(
          new DummyManifestMatcher(),
          runnerFactory: _dummyRunnerFactory);
      final Session session = await handler.createSession(recipe);
      final MutationHelper graph = new MutationHelper(session.graph);

      final Edge e1 = graph.addEdge(session.root, l1, null);
      /* final Edge e2 = */ graph.addEdge(e1.target, l2, null);
      /* final Edge e3 = */ graph.addEdge(e1.target, l3, null);

      final Edge e4 = graph.addEdge(session.root, l1, null);
      /* final Edge e5 = */ graph.addEdge(e4.target, l2, null);
      /* final Edge e6 = */ graph.addEdge(e4.target, l3, null);

      final Edge e7 = graph.addEdge(session.root, l1, null);
      /* final Edge e8 = */ graph.addEdge(e7.target, l2, null);
      /* final Edge e9 = */ graph.addEdge(e7.target, l3, null);

      session.start();

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(2));
      expect(session.modules[0].instances.length, equals(1));
      expect(session.modules[0].instances.fold(0, _countNotNull), equals(1));
    });

    test('Input paths with common roots, multiple leaf nodes', () async {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input:
            - p1 -> p2
            - p1 -> p3

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
      ''';
      final Recipe recipe = parseRecipe(recipeText);
      final Handler handler = new Handler.fromManifestMatcher(
          new DummyManifestMatcher(),
          runnerFactory: _dummyRunnerFactory);
      final Session session = await handler.createSession(recipe);
      final MutationHelper graph = new MutationHelper(session.graph);

      final Edge e1 = graph.addEdge(session.root, l1, null);
      /* final Edge e2 = */ graph.addEdge(e1.target, l2, null);
      /* final Edge e3 = */ graph.addEdge(e1.target, l3, null);

      /* final Edge e5 = */ graph.addEdge(e1.target, l2, null);
      /* final Edge e6 = */ graph.addEdge(e1.target, l3, null);

      /* final Edge e8 = */ graph.addEdge(e1.target, l2, null);
      /* final Edge e9 = */ graph.addEdge(e1.target, l3, null);

      session.start();

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(2));
      expect(session.modules[0].instances.length, equals(1));
      expect(session.modules[0].instances.fold(0, _countNotNull), equals(1));
    });

    test('Input paths with repeated/singular roots, multiple leaf nodes',
        () async {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input:
            - p1+ -> p2
            - p1 -> p3

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
      ''';
      final Recipe recipe = parseRecipe(recipeText);
      final Handler handler = new Handler.fromManifestMatcher(
          new DummyManifestMatcher(),
          runnerFactory: _dummyRunnerFactory);
      final Session session = await handler.createSession(recipe);
      final MutationHelper graph = new MutationHelper(session.graph);

      final Edge e1 = graph.addEdge(session.root, l1, null);
      /* final Edge e2 = */ graph.addEdge(e1.target, l2, null);
      /* final Edge e3 = */ graph.addEdge(e1.target, l3, null);

      /* final Edge e5 = */ graph.addEdge(e1.target, l2, null);
      /* final Edge e6 = */ graph.addEdge(e1.target, l3, null);

      /* final Edge e8 = */ graph.addEdge(e1.target, l2, null);
      /* final Edge e9 = */ graph.addEdge(e1.target, l3, null);

      session.start();

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(2));
      expect(session.modules[0].instances.length, equals(1));
      expect(session.modules[0].instances.fold(0, _countNotNull), equals(1));
    });

    test('Input paths with repeated root include partial matches', () async {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input:
            - p1+ -> p2

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
      ''';
      final Recipe recipe = parseRecipe(recipeText);
      final Handler handler = new Handler.fromManifestMatcher(
          new DummyManifestMatcher(),
          runnerFactory: _dummyRunnerFactory);
      final Session session = await handler.createSession(recipe);
      final MutationHelper graph = new MutationHelper(session.graph);

      final Edge e1 = graph.addEdge(session.root, l1, null);
      final Edge e2 = graph.addEdge(e1.target, l2, null);
      final Edge e3 = graph.addEdge(session.root, l1, null);

      session.start();

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(1));
      expect(session.modules[0].instances.length, equals(1));
      final ModuleInstance instance = session.modules[0].instances[0];
      expect(instance.inputMatches.length, equals(1));
      expect(instance.anchorMatches[0].pathExpr.properties.length, equals(2));
      // There is only one p2 edge, so the second component only appears once.
      expect(instance.anchorMatches[0].edgeList(1).length, equals(1));
      expect(instance.anchorMatches[0].edgeList(1)[0], equals(e2));
      // There are two p1 edges (e1 and e3), but only one has a p2 edge as
      // descendant (e1).
      expect(instance.anchorMatches[0].edgeList(0).length, equals(2));
      expect(instance.anchorMatches[0].edgeList(0)[0], equals(e1));
      expect(instance.anchorMatches[0].edgeList(0)[1], equals(e3));
    });

    test('Module instance recreates when edge changes', () async {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input:
            - p1 -> p2

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
      ''';
      final Recipe recipe = parseRecipe(recipeText);
      final Handler handler = new Handler.fromManifestMatcher(
          new DummyManifestMatcher(),
          runnerFactory: _dummyRunnerFactory);
      final Session session = await handler.createSession(recipe);
      final MutationHelper graph = new MutationHelper(session.graph);
      final Edge e1 = graph.addEdge(session.root, l1, null);
      final Edge e2 = graph.addEdge(e1.target, l2, null);
      session.start();

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(1));
      expect(session.modules[0].inputs[0].matches.length, equals(1));
      expect(session.modules[0].instances.length, equals(1));
      final ModuleInstance instance = session.modules[0].instances[0];
      // p1
      expect(instance.anchorMatches[0].edgeList(0).length, equals(1));
      expect(instance.anchorMatches[0].edgeList(0)[0], equals(e1));
      // p2
      expect(instance.anchorMatches[0].edgeList(1).length, equals(1));
      expect(instance.anchorMatches[0].edgeList(1)[0], equals(e2));

      // Delete an edge and add a new edge with that label.
      Edge e2Replaced;
      session.graph.mutate((GraphMutator mutator) {
        mutator.removeEdge(e2.id);
        e2Replaced = mutator.addEdge(e1.target.id, l2);
      });

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(1));
      expect(session.modules[0].inputs[0].matches.length, equals(1));
      expect(session.modules[0].instances.length, equals(1));
      // Check a new instance is created.
      expect(session.modules[0].instances[0] == instance, isFalse);

      final ModuleInstance instanceNew = session.modules[0].instances[0];
      // p1
      expect(instanceNew.anchorMatches[0].edgeList(0).length, equals(1));
      expect(instanceNew.anchorMatches[0].edgeList(0)[0], equals(e1));
      // p2
      expect(instanceNew.anchorMatches[0].edgeList(1)[0], isNotNull);
      expect(instanceNew.anchorMatches[0].edgeList(1)[0], equals(e2Replaced));
    });

    test('Input paths from compose field', () async {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           compose:
            - p1* -> p2

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
      ''';
      final Recipe recipe = parseRecipe(recipeText);
      final Handler handler = new Handler.fromManifestMatcher(
          new DummyManifestMatcher(),
          runnerFactory: _dummyRunnerFactory);
      final Session session = await handler.createSession(recipe);
      final Graph graph = session.graph;
      session.start();

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(1));
      expect(session.modules[0].instances.length, equals(1));
      final ModuleInstance instance = session.modules[0].instances[0];
      expect(instance.anchorMatches.length, equals(1));
      expect(instance.anchorMatches[0].isEmpty, isTrue);

      Edge e1, e2;
      graph.mutate((GraphMutator mutator) {
        e1 = mutator.addEdge(session.root.id, l1);
        e2 = mutator.addEdge(e1.target.id, l2);
      });

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(1));
      expect(session.modules[0].inputs[0].matches.length, equals(1));
      expect(session.modules[0].instances.length, equals(1));
      expect(session.modules[0].instances[0] == instance, isTrue);
      // p1
      expect(instance.anchorMatches[0].edgeList(0).length, equals(1));
      expect(instance.anchorMatches[0].edgeList(0)[0], equals(e1));
      // p2
      expect(instance.anchorMatches[0].edgeList(1).length, equals(1));
      expect(instance.anchorMatches[0].edgeList(1)[0], equals(e2));
    });

    test('Optional input paths', () async {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input:
           - p1? -> p2

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
      ''';
      final Recipe recipe = parseRecipe(recipeText);
      final Handler handler = new Handler.fromManifestMatcher(
          new DummyManifestMatcher(),
          runnerFactory: _dummyRunnerFactory);
      final Session session = await handler.createSession(recipe);
      final MutationHelper graph = new MutationHelper(session.graph);
      session.start();

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(1));
      expect(session.modules[0].instances.length, equals(1));
      final ModuleInstance instance = session.modules[0].instances[0];
      expect(instance.anchorMatches.length, equals(1));
      expect(instance.anchorMatches[0].isEmpty, isTrue);

      final Edge e1 = graph.addEdge(session.root, l1, null, id: "e1");
      final Edge e2 = graph.addEdge(e1.target, l2, null, id: "e2");

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(1));
      expect(session.modules[0].inputs[0].matches.length, equals(1));
      expect(session.modules[0].instances.length, equals(1));
      expect(session.modules[0].instances[0] == instance, isTrue);
      // p1
      expect(instance.anchorMatches[0].edgeList(0).length, equals(1));
      expect(instance.anchorMatches[0].edgeList(0)[0], equals(e1));
      // p2
      expect(instance.anchorMatches[0].edgeList(1).length, equals(1));
      expect(instance.anchorMatches[0].edgeList(1)[0], equals(e2));

      // Delete an edge. Deleting an edge that is matched by an optional input
      // does not delete the instance.
      graph.removeEdge(e2);

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(1));
      expect(session.modules[0].inputs[0].matches.length, equals(1));
      expect(session.modules[0].instances.length, equals(1));
      expect(session.modules[0].instances[0] == instance, isTrue);
      // p1
      expect(instance.anchorMatches[0].edgeList(0).length, equals(1));
      expect(instance.anchorMatches[0].edgeList(0)[0], equals(e1));
      // p2
      expect(instance.anchorMatches[0].edgeList(1).length, equals(0));

      // Insert new edge again under e1, reuses the same instance.
      final Edge e3 = graph.addEdge(e1.target, l2, null);

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(1));
      expect(session.modules[0].inputs[0].matches.length, equals(1));
      expect(session.modules[0].instances.length, equals(1));
      expect(session.modules[0].instances[0] == instance, isTrue);
      // p1
      expect(instance.anchorMatches[0].edgeList(0).length, equals(1));
      expect(instance.anchorMatches[0].edgeList(0)[0], equals(e1));
      // p2 (e3)
      expect(instance.anchorMatches[0].edgeList(1).length, equals(1));
      expect(instance.anchorMatches[0].edgeList(1)[0], equals(e3));

      // Delete both edges.
      graph.removeEdge(e3);
      graph.removeEdge(e1);

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(1));
      expect(session.modules[0].inputs[0].matches.length, equals(1));
      expect(session.modules[0].instances.length, equals(1));
      expect(session.modules[0].instances[0] == instance, isTrue);
      // p1
      expect(instance.anchorMatches[0].edgeList(0).length, equals(0));

      // Insert both edges again reuses the old match and instance.
      final Edge e4 = graph.addEdge(session.root, l1, null);
      graph.addEdge(e4.target, l2, null);

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(1));
      expect(session.modules[0].inputs[0].matches.length, equals(1));
      expect(session.modules[0].instances.length, equals(1));
      expect(session.modules[0].instances[0] == instance, isTrue);
    });

    test('Two optional input paths', () async {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input:
            - p1?
            - p2?

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
      ''';
      final Recipe recipe = parseRecipe(recipeText);
      final Handler handler = new Handler.fromManifestMatcher(
          new DummyManifestMatcher(),
          runnerFactory: _dummyRunnerFactory);
      final Session session = await handler.createSession(recipe);
      final MutationHelper graph = new MutationHelper(session.graph);
      session.start();

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(2));
      expect(session.modules[0].instances.length, equals(1));
      final ModuleInstance instance = session.modules[0].instances[0];
      expect(instance.anchorMatches.length, equals(2));
      expect(instance.anchorMatches[0].isEmpty, isTrue);
      expect(instance.anchorMatches[1].isEmpty, isTrue);

      final Edge e1 = graph.addEdge(session.root, l1, null);

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(2));
      expect(session.modules[0].instances.length, equals(1));
      expect(session.modules[0].instances[0] == instance, isTrue);
      expect(instance.anchorMatches.length, equals(2));
      // p1
      expect(instance.anchorMatches[0].pathExpr.properties.length, equals(1));
      expect(instance.anchorMatches[0].edgeList(0).length, equals(1));
      expect(instance.anchorMatches[0].edgeList(0)[0], equals(e1));
      // p2
      expect(instance.anchorMatches[1].isEmpty, isTrue);

      // Add edge for p2.
      final Edge e2 = graph.addEdge(session.root, l2, null);

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(2));
      expect(session.modules[0].instances.length, equals(1));
      expect(session.modules[0].instances[0] == instance, isTrue);
      expect(instance.anchorMatches.length, equals(2));
      // p1
      expect(instance.anchorMatches[0].edgeList(0).length, equals(1));
      expect(instance.anchorMatches[0].pathExpr.properties.length, equals(1));
      expect(instance.anchorMatches[0].edgeList(0)[0], equals(e1));
      // p2
      expect(instance.anchorMatches[1].pathExpr.properties.length, equals(1));
      expect(instance.anchorMatches[1].edgeList(0).length, equals(1));
      expect(instance.anchorMatches[1].edgeList(0)[0], equals(e2));
    });

    test('One optional and one required input paths', () async {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input:
            - p1
            - p2?

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
      ''';
      final Recipe recipe = parseRecipe(recipeText);
      final Handler handler = new Handler.fromManifestMatcher(
          new DummyManifestMatcher(),
          runnerFactory: _dummyRunnerFactory);
      final Session session = await handler.createSession(recipe);
      final MutationHelper graph = new MutationHelper(session.graph);
      session.start();

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(2));
      expect(session.modules[0].instances.length, equals(1));
      expect(session.modules[0].instances[0], isNull);

      final Edge e1 = graph.addEdge(session.root, l1, null);

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(2));
      expect(session.modules[0].instances.length, equals(1));
      final ModuleInstance instance = session.modules[0].instances[0];
      expect(instance.anchorMatches.length, equals(2));
      // p1
      expect(instance.anchorMatches[0].pathExpr.properties.length, equals(1));
      expect(instance.anchorMatches[0].edgeList(0).length, equals(1));
      expect(instance.anchorMatches[0].edgeList(0)[0], equals(e1));
      // p2
      expect(instance.anchorMatches[1].isEmpty, isTrue);
    });

    test('One optional and one required input paths', () async {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input:
            - p1
            - p2?

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
      ''';
      final Recipe recipe = parseRecipe(recipeText);
      final Handler handler = new Handler.fromManifestMatcher(
          new DummyManifestMatcher(),
          runnerFactory: _dummyRunnerFactory);
      final Session session = await handler.createSession(recipe);
      final MutationHelper graph = new MutationHelper(session.graph);
      session.start();

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(2));
      expect(session.modules[0].instances.length, equals(1));
      expect(session.modules[0].instances[0], isNull);

      final Edge e1 = graph.addEdge(session.root, l1, null);

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(2));
      expect(session.modules[0].instances.length, equals(1));
      final ModuleInstance instance = session.modules[0].instances[0];
      expect(instance.anchorMatches.length, equals(2));
      // p1
      expect(instance.anchorMatches[0].pathExpr.properties.length, equals(1));
      expect(instance.anchorMatches[0].edgeList(0).length, equals(1));
      expect(instance.anchorMatches[0].edgeList(0)[0], equals(e1));
      // p2
      expect(instance.anchorMatches[1].isEmpty, isTrue);
    });

    test('One optional input with optionality at non-first level', () async {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input:
            - p1 -> p2? -> p3

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
      ''';
      final Recipe recipe = parseRecipe(recipeText);
      final Handler handler = new Handler.fromManifestMatcher(
          new DummyManifestMatcher(),
          runnerFactory: _dummyRunnerFactory);
      final Session session = await handler.createSession(recipe);
      final MutationHelper graph = new MutationHelper(session.graph);
      session.start();

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(1));
      expect(session.modules[0].instances.length, equals(1));
      expect(session.modules[0].instances[0], isNull);

      final SessionPattern input = session.modules[0].inputs[0];
      // Check for no edges in the match.
      expect(input.matches[0].pathExpr.properties.length, equals(3));
      expect(input.matches[0].isEmpty, isTrue);
      expect(input.matches[0].edgeList(0).length, equals(0));

      final Edge e1 = graph.addEdge(session.root, l1, null);

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(1));
      expect(session.modules[0].instances.length, equals(1));

      final ModuleInstance instance = session.modules[0].instances[0];
      expect(instance.anchorMatches[0].pathExpr.properties.length, equals(3));
      // Check for no edges in the match yet as its not completely matched.
      expect(instance.anchorMatches[0].edgeList(0).length, equals(1));
      expect(instance.anchorMatches[0].edgeList(1).length, equals(0));

      // Inserting a new p1 edge shouldn't create another instance even though
      // the input is optional. Only the first instance can be created with no
      // or partial values. There after new instances can be created only on
      // full input values.
      /* final Edge e11 = */ graph.addEdge(session.root, l1, null);

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(1));
      expect(session.modules[0].instances.length, equals(1));
      expect(session.modules[0].instances[0] == instance, isTrue);

      // Create edges for p2 and p3 on edge e1 and check if edges are updates in
      // the instance match.
      final Edge e2 = graph.addEdge(e1.target, l2, null);
      final Edge e3 = graph.addEdge(e2.target, l3, null);

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(1));
      expect(session.modules[0].instances.length, equals(1));
      expect(session.modules[0].instances[0] == instance, isTrue);
      // Check for all edges in the instance match.
      // p1
      expect(instance.anchorMatches[0].edgeList(0).length, equals(1));
      expect(instance.anchorMatches[0].edgeList(0)[0], equals(e1));
      // Check for p2
      expect(instance.anchorMatches[0].edgeList(1).length, equals(1));
      expect(instance.anchorMatches[0].edgeList(1)[0], equals(e2));
      // Check for p3
      expect(instance.anchorMatches[0].edgeList(2).length, equals(1));
      expect(instance.anchorMatches[0].edgeList(2)[0], equals(e3));
    });

    test('One optional-repeated input path', () async {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input:
            - p1 -> p2*

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
      ''';
      final Recipe recipe = parseRecipe(recipeText);
      final Handler handler = new Handler.fromManifestMatcher(
          new DummyManifestMatcher(),
          runnerFactory: _dummyRunnerFactory);
      final Session session = await handler.createSession(recipe);
      final MutationHelper graph = new MutationHelper(session.graph);
      session.start();

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(1));
      expect(session.modules[0].instances.length, equals(1));
      expect(session.modules[0].instances[0], isNull);

      // Check for no edges in the match.
      final SessionPattern input = session.modules[0].inputs[0];
      expect(input.matches[0].pathExpr.properties.length, equals(2));
      expect(input.matches[0].isEmpty, isTrue);
      expect(input.matches[0].edgeList(0).length, equals(0));

      final Edge e1 = graph.addEdge(session.root, l1, null);

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(1));
      expect(session.modules[0].instances.length, equals(1));

      final ModuleInstance instance = session.modules[0].instances[0];
      expect(instance.anchorMatches[0].pathExpr.properties.length, equals(2));
      expect(instance.anchorMatches[0].edgeList(0).length, equals(1));
      expect(instance.anchorMatches[0].edgeList(1).length, equals(0));

      // Inserting a new p1 edge shouldn't create another instance even though
      // the input is optional. Only the first instance can be created with no
      // or partial values. There after new instances can be created only on
      // full input values.
      final Edge e11 = graph.addEdge(session.root, l1, null);

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(1));
      expect(session.modules[0].instances.length, equals(1));
      expect(session.modules[0].instances[0] == instance, isTrue);

      // Create two edges for p2 on edge e1 and check if edges are updates in
      // the instance match.
      final Edge e2 = graph.addEdge(e1.target, l2, null);
      final Edge e3 = graph.addEdge(e1.target, l2, null);

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(1));
      expect(session.modules[0].instances.length, equals(1));
      expect(session.modules[0].instances[0] == instance, isTrue);
      // Check for all edges in the instance match.
      // p1
      expect(instance.anchorMatches[0].edgeList(0).length, equals(1));
      expect(instance.anchorMatches[0].edgeList(0)[0], equals(e1));
      // Check for p2
      expect(instance.anchorMatches[0].edgeList(1).length, equals(2));
      expect(instance.anchorMatches[0].edgeList(1)[0], equals(e2));
      expect(instance.anchorMatches[0].edgeList(1)[1], equals(e3));

      // Create an edge for p2 on edge e11 and check a new instance is created.
      graph.addEdge(e11.target, l2, null);

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(1));
      expect(session.modules[0].instances.length, equals(1));
      expect(session.modules[0].instances[0] == instance, isTrue);
    });

    test('Optional-repeated input path with prefix matching', () async {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           scope: p1
           input:
            - p1 -> p2*
            - p1 -> p3

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
      ''';
      final Recipe recipe = parseRecipe(recipeText);
      final Handler handler = new Handler.fromManifestMatcher(
          new DummyManifestMatcher(),
          runnerFactory: _dummyRunnerFactory);
      final Session session = await handler.createSession(recipe);
      final MutationHelper graph = new MutationHelper(session.graph);
      session.start();

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(2));
      expect(session.modules[0].instances.length, equals(1));
      expect(session.modules[0].instances[0], isNull);

      final Edge rootp1node1 = graph.addEdge(
          session.root, l1, graph.addNode(id: 'node1'),
          id: 'rootp1node1');
      final Node node1 = rootp1node1.target;

      final Edge node1p3node2 = graph
          .addEdge(node1, l3, graph.addNode(id: 'node2'), id: 'rootp3node2');

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(2));
      expect(session.modules[0].instances.length, equals(1));
      expect(session.modules[0].instances[0], isNotNull);

      // Inserting another p1 edge creates another match of the scope, but it
      // does not create another instance because the p1 edge of the first input
      // is not optional so the match is not complete.
      final Edge rootp1node3 = graph.addEdge(
          session.root, l1, graph.addNode(id: 'node3'),
          id: 'rootp1node3');
      final Node node3 = rootp1node3.target;

      expect(session.modules.length, equals(1));
      expect(session.modules[0].scope, isNotNull);
      expect(session.modules[0].inputs.length, equals(2));
      expect(session.modules[0].instances.length, equals(2));
      expect(session.modules[0].instances[0], isNotNull);
      expect(session.modules[0].instances[1], isNull);

      // Inserting more p2 edges in the first input updates the inputs of the
      // existing module instance, but does neither create new matches nor new
      // instances, because the edge expression is optional repeated.
      final Edge node1p2node4 = graph
          .addEdge(node1, l2, graph.addNode(id: 'node4'), id: 'node1p2node4');
      final Edge node1p2node5 = graph
          .addEdge(node1, l2, graph.addNode(id: 'node5'), id: 'node1p2node5');

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(2));
      expect(session.modules[0].instances.length, equals(2));
      final ModuleInstance instance0 = session.modules[0].instances[0];
      expect(instance0, isNotNull);
      expect(instance0.anchorMatches.length, equals(2));
      // p1
      expect(instance0.anchorMatches[0].edgeList(0).length, equals(1));
      expect(instance0.anchorMatches[0].edgeList(0)[0], equals(rootp1node1));
      // p2
      expect(instance0.anchorMatches[0].edgeList(1).length, equals(2));
      expect(instance0.anchorMatches[0].edgeList(1)[0], equals(node1p2node4));
      expect(instance0.anchorMatches[0].edgeList(1)[1], equals(node1p2node5));
      // p3
      expect(instance0.anchorMatches[1].edgeList(0).length, equals(1));
      expect(instance0.anchorMatches[1].edgeList(0)[0], equals(rootp1node1));
      expect(instance0.anchorMatches[1].edgeList(1).length, equals(1));
      expect(instance0.anchorMatches[1].edgeList(1)[0], equals(node1p3node2));

      // Create another p2. This is matched by both scope and input such that
      // both a new match is created and a new instance.
      final Edge node3p2node6 = graph
          .addEdge(node3, l2, graph.addNode(id: 'node6'), id: 'node3p2node6');
      final Edge node3p3node7 = graph
          .addEdge(node3, l3, graph.addNode(id: 'node7'), id: 'node3p3node7');

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(2));
      expect(session.modules[0].instances.length, equals(2));
      expect(session.modules[0].instances[0], equals(instance0));

      final ModuleInstance instance1 = session.modules[0].instances[1];
      expect(instance1, isNotNull);
      expect(instance1.anchorMatches.length, equals(2));

      // p1
      expect(instance1.anchorMatches[0].edgeList(0).length, equals(1));
      expect(instance1.anchorMatches[0].edgeList(0)[0], equals(rootp1node3));
      // p2
      expect(instance1.anchorMatches[0].edgeList(1).length, equals(1));
      expect(instance1.anchorMatches[0].edgeList(1)[0], equals(node3p2node6));
      // p3
      expect(instance1.anchorMatches[1].edgeList(0).length, equals(1));
      expect(instance1.anchorMatches[1].edgeList(0)[0], equals(rootp1node3));
      expect(instance1.anchorMatches[1].edgeList(1).length, equals(1));
      expect(instance1.anchorMatches[1].edgeList(1)[0], equals(node3p3node7));
    });

    test('Fork session', () async {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input: p1

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
      ''';
      final Recipe recipe = parseRecipe(recipeText);
      final Handler handler = new Handler.fromManifestMatcher(
          new DummyManifestMatcher(),
          runnerFactory: _dummyRunnerFactory);
      final Session session = await handler.createSession(recipe);
      session.start();

      // Fork the session above.
      final Session forkSession = await handler.forkSession(session.id);
      forkSession.start();

      expect(await handler.findSession(session.id), equals(session));
      expect(await handler.findSession(forkSession.id), equals(forkSession));

      // Sessions should be identical.
      expect(session.graph.nodes, equals(forkSession.graph.nodes));
      expect(session.graph.edges, equals(forkSession.graph.edges));
      expect(session.graph.root, equals(forkSession.graph.root));
      expect(
          session.graph.metadataNode, equals(forkSession.graph.metadataNode));

      // The fork session should have an empty recipe.
      expect(session.metadata.getRecipe().steps.isEmpty, isFalse);
      expect(forkSession.metadata.getRecipe().steps.isEmpty, isTrue);

      final MutationHelper helper = new MutationHelper(session.graph);
      final MutationHelper forkHelper = new MutationHelper(forkSession.graph);

      // Changes to the parent session are reflected in the child session.
      helper.addNode();
      expect(session.graph.nodes, equals(forkSession.graph.nodes));

      // Changes to the child session are NOT reflected in the parent session.
      forkHelper.addNode();
      expect(session.graph.nodes, isNot(equals(forkSession.graph.nodes)));

      // The forked session cannot be started once stopped.
      expect(forkSession.isStarted, isTrue);
      forkSession.stop();
      expect(forkSession.isStarted, isFalse);
      forkSession.start();
      expect(forkSession.isStarted, isFalse);

      // Once stopped, the handler forgets about the child session.
      session.stop();
      expect(await handler.findSession(session.id), equals(session));
      expect(await handler.findSession(forkSession.id), isNull);

      expect(handler.restoreSession(forkSession.id),
          throwsA(new isInstanceOf<GraphUnavailable>()));
    });
  });

  group('Module runner', () {
    test('Run modules with no input', () async {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           output:
            - p1

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
      ''';
      final String manifestText = '''
        verb: v1
        output:
         - p1

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
      ''';

      final Recipe recipe = parseRecipe(recipeText);
      final RecordingModuleRunner runner = new RecordingModuleRunner();
      final ModuleRunnerFactory runnerFactory = () => runner;
      final Manifest manifest = parseManifest(manifestText);
      final Handler handler =
          new Handler(manifests: [manifest], runnerFactory: runnerFactory);
      final Session session = await handler.createSession(recipe);
      final MutationHelper graph = new MutationHelper(session.graph);

      session.start();

      expect(session.modules.length, equals(1));

      // verb: v1
      expect(session.modules[0].inputs.length, equals(0));

      // The module should have been started.
      expect(runner.instance, isNotNull);
      expect(runner.updateCallCount, equals(1));

      /* final Edge e1 = */ graph.addEdge(session.root, l2, null);

      // The module should have been updated, as a new p1 edge has been added to
      // the graph.
      expect(runner.updateCallCount, equals(1));
    });

    test('Init and update repeated inputs on module runners', () async {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input:
            - p1+

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
      ''';
      final String manifestText = '''
        verb: v1
        input:
         - p1+
        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
      ''';

      final Recipe recipe = parseRecipe(recipeText);
      final RecordingModuleRunner runner = new RecordingModuleRunner();
      final ModuleRunnerFactory runnerFactory = () => runner;
      final Manifest manifest = parseManifest(manifestText);
      final Handler handler =
          new Handler(manifests: [manifest], runnerFactory: runnerFactory);
      final Session session = await handler.createSession(recipe);
      final MutationHelper graph = new MutationHelper(session.graph);

      final Edge e1 = graph.addEdge(session.root, l1, null);

      session.start();

      expect(session.modules.length, equals(1));

      // verb: v1
      expect(session.modules[0].inputs.length, equals(1));

      // verb: v1, input: p1
      expect(session.modules[0].inputs[0].matches.length, equals(1));
      expect(session.modules[0].inputs[0].matches[0].target(0, 0),
          equals(e1.target));
      expect(session.modules[0].inputs[0].matches[0].isComplete, isTrue);

      // The module should have been started.
      expect(runner.instance, isNotNull);
      expect(runner.updateCallCount, equals(1));

      // Add a new p1 edge to the graph.
      /* final Edge e2 = */ graph.addEdge(session.root, l1, graph.addNode());

      // The module should have been updated, as a new p1 edge has been added to
      // the graph.
      expect(runner.updateCallCount, equals(2));

      // Add a new edge with a different label.
      /* final Edge e3 = */ graph.addEdge(session.root, l2, null);

      // The module should not be updated, as no new p1 edge has been added to
      // the graph.
      expect(runner.updateCallCount, equals(2));
    });

    test('Init and update repeated inputs on multiple module runners',
        () async {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           scope: p2
           input:
            - p1+
            - p2

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
      ''';
      final String manifestText = '''
        verb: v1
        input:
         - p1+
         - p2
        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
      ''';

      final Recipe recipe = parseRecipe(recipeText);

      final List<RecordingModuleRunner> runners = [
        new RecordingModuleRunner(),
        new RecordingModuleRunner()
      ];
      final Incrementer incrementer = new Incrementer();
      final ModuleRunnerFactory runnerFactory = () => runners[incrementer.next];

      final Manifest manifest = parseManifest(manifestText);
      final Handler handler =
          new Handler(manifests: [manifest], runnerFactory: runnerFactory);
      final Session session = await handler.createSession(recipe);
      final MutationHelper graph = new MutationHelper(session.graph);

      final Edge e1 = graph.addEdge(session.root, l1, null);
      final Edge e2 = graph.addEdge(session.root, l2, null);
      final Edge e3 = graph.addEdge(session.root, l2, null);

      session.start();

      expect(session.modules.length, equals(1));

      // verb: v1
      expect(session.modules[0].scope, isNotNull);
      expect(session.modules[0].inputs.length, equals(2));

      // scope: p2
      expect(session.modules[0].scope.matches.length, equals(2));
      expect(
          session.modules[0].scope.matches[0].target(0, 0), equals(e2.target));
      expect(session.modules[0].scope.matches[0].isComplete, isTrue);
      expect(
          session.modules[0].scope.matches[1].target(0, 0), equals(e3.target));
      expect(session.modules[0].scope.matches[1].isComplete, isTrue);

      // input: p1+
      expect(session.modules[0].inputs[0].matches.length, equals(1));
      expect(session.modules[0].inputs[0].matches[0].target(0, 0),
          equals(e1.target));
      expect(session.modules[0].inputs[0].matches[0].isComplete, isTrue);

      // input: p2
      expect(session.modules[0].inputs[1].matches.length, equals(2));
      expect(session.modules[0].inputs[1].matches[0].target(0, 0),
          equals(e2.target));
      expect(session.modules[0].inputs[1].matches[0].isComplete, isTrue);
      expect(session.modules[0].inputs[1].matches[1].target(0, 0),
          equals(e3.target));
      expect(session.modules[0].inputs[1].matches[1].isComplete, isTrue);

      // The module instances are started.
      expect(session.modules[0].instances.length, equals(2));
      expect(runners[0].instance, isNotNull);
      expect(runners[0].updateCallCount, equals(1));
      expect(runners[1].instance, isNotNull);
      expect(runners[1].updateCallCount, equals(1));

      // Add a new p1 edge to the graph. NOTE(mesch): If this is not inside
      // mutate(), then the update count is higher, presumably because
      // bookkeeping information is added to the session too (I'm not quite
      // sure).
      session.graph.mutate(
          (GraphMutator mutator) => mutator.addEdge(session.root.id, l1));

      // Both modules should have been updated, as a new p1 edge has been added
      // to the graph.
      expect(runners[0].updateCallCount, equals(2));
      expect(runners[1].updateCallCount, equals(2));

      // Add a new edge with a different label.
      /* final Edge e5 = */ graph.addEdge(session.root, l3, null);

      // No module should be updated, as no new p1 edge has been added to the
      // graph.
      expect(runners[0].updateCallCount, equals(2));
      expect(runners[1].updateCallCount, equals(2));
    });

    test('Init with representation types', () async {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input: p1
        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
      ''';
      final String manifestText = '''
        verb: v1
        input: p1 <p2>
        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
      ''';

      final Recipe recipe = parseRecipe(recipeText);
      final RecordingModuleRunner runner = new RecordingModuleRunner();
      final ModuleRunnerFactory runnerFactory = () => runner;
      final Manifest manifest = parseManifest(manifestText);
      final Handler handler =
          new Handler(manifests: [manifest], runnerFactory: runnerFactory);
      final Session session = await handler.createSession(recipe);
      final MutationHelper graph = new MutationHelper(session.graph);

      final Edge e1 = graph.addEdge(session.root, l1, null);
      graph.setValue(e1.target, l2.single, new Uint8List(0));

      session.start();

      expect(session.modules.length, equals(1));

      // The module should have been started.
      expect(runner.instance, isNotNull);
      expect(runner.updateCallCount, equals(1));
    });

    test('Update with representation types', () async {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input: p1

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
      ''';
      final String manifestText = '''
        verb: v1
        input: p1 <p2>
        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
      ''';

      final Recipe recipe = parseRecipe(recipeText);
      final RecordingModuleRunner runner = new RecordingModuleRunner();
      final ModuleRunnerFactory runnerFactory = () => runner;
      final Manifest manifest = parseManifest(manifestText);
      final Handler handler =
          new Handler(manifests: [manifest], runnerFactory: runnerFactory);
      final Session session = await handler.createSession(recipe);
      final MutationHelper graph = new MutationHelper(session.graph);

      final Edge e1 = graph.addEdge(session.root, l1, null);

      session.start();

      expect(session.modules.length, equals(1));

      // The module should have been started, even without a represenation
      // value.
      expect(runner.instance, isNotNull);

      expect(runner.updateCallCount, equals(1));

      // Add representation value to the node.
      graph.setValue(e1.target, l2.single, BuiltinString.write(""));

      // The module should have been updated, as the representation value has
      // been added to node at e1.target
      expect(runner.updateCallCount, equals(2));
    });

    test('Init with double-declared representation types', () async {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input: p1 <p2>

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
      ''';
      final String manifestText = '''
        verb: v1
        input:
        - p1 <p2>
        use:
        - v1: http://tq.io/v1
        - p1: http://tq.io/p1
        - p2: http://tq.io/p2
      ''';

      final Recipe recipe = parseRecipe(recipeText);
      final RecordingModuleRunner runner = new RecordingModuleRunner();
      final ModuleRunnerFactory runnerFactory = () => runner;
      final Manifest manifest = parseManifest(manifestText);
      final Handler handler =
          new Handler(manifests: [manifest], runnerFactory: runnerFactory);
      final Session session = await handler.createSession(recipe);
      final MutationHelper graph = new MutationHelper(session.graph);

      final Edge e1 = graph.addEdge(session.root, l1, null);
      graph.setValue(e1.target, l2.single, new Uint8List(0));

      session.start();

      expect(session.modules.length, equals(1));
      expect(session.modules[0].inputs.length, equals(1));
      expect(session.modules[0].inputs[0].matches.length, equals(1));
      expect(session.modules[0].inputs[0].matches[0].length, equals(1));

      // The module should have been started.
      expect(runner.instance, isNotNull);
      expect(runner.updateCallCount, equals(1));
    });
  });

  group('deletion', () {
    test('Single input deletion', () async {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           scope: p2
           input:
            - p1
            - p2 -> p3

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
      ''';
      final Recipe recipe = parseRecipe(recipeText);
      final Handler handler = new Handler.fromManifestMatcher(
          new DummyManifestMatcher(),
          runnerFactory: _dummyRunnerFactory);
      final Session session = await handler.createSession(recipe);
      final MutationHelper graph = new MutationHelper(session.graph);

      /* final Edge e1 = */ graph.addEdge(session.root, l1, null);
      final Edge e2 = graph.addEdge(session.root, l2, null);
      /* final Edge e3 = */ graph.addEdge(e2.target, l3, null);
      final Edge e4 = graph.addEdge(session.root, l2, null);
      final Edge e5 = graph.addEdge(e4.target, l3, null);

      session.start();
      expect(session.modules.length, equals(1));

      expect(session.modules[0].instances.length, equals(2));
      expect(session.modules[0].instances[0], isNotNull);
      expect(session.modules[0].instances[1], isNotNull);

      expect(session.modules[0].scope.matches.length, equals(2));
      expect(session.modules[0].scope.matches[0].isEmpty, isFalse);
      expect(session.modules[0].scope.matches[0].isComplete, isTrue);
      expect(session.modules[0].scope.matches[1].isEmpty, isFalse);
      expect(session.modules[0].scope.matches[1].isComplete, isTrue);

      expect(session.modules[0].inputs[1].matches.length, equals(2));
      expect(session.modules[0].inputs[1].matches[0].isComplete, isTrue);
      expect(session.modules[0].inputs[1].matches[1].isComplete, isTrue);

      graph.removeEdge(e5);

      expect(session.modules[0].instances.length, equals(2));
      expect(session.modules[0].instances[0], isNotNull);
      expect(session.modules[0].instances[1], isNull);

      expect(session.modules[0].scope.matches.length, equals(2));
      expect(session.modules[0].scope.matches[0].isEmpty, isFalse);
      expect(session.modules[0].scope.matches[0].isComplete, isTrue);
      expect(session.modules[0].scope.matches[1].isEmpty, isFalse);
      expect(session.modules[0].scope.matches[1].isComplete, isTrue);

      expect(session.modules[0].inputs[1].matches.length, equals(2));
      expect(session.modules[0].inputs[1].matches[0].isComplete, isTrue);
      expect(session.modules[0].inputs[1].matches[1].isComplete, isFalse);

      // If the scope match becomes empty, the instance and match is removed,
      // not just set to null.
      graph.removeEdge(e4);

      expect(session.modules[0].instances.length, equals(1));
      expect(session.modules[0].instances[0], isNotNull);

      expect(session.modules[0].scope.matches.length, equals(1));
      expect(session.modules[0].scope.matches[0].isEmpty, isFalse);
      expect(session.modules[0].scope.matches[0].isComplete, isTrue);

      expect(session.modules[0].inputs[1].matches.length, equals(1));
      expect(session.modules[0].inputs[1].matches[0].isComplete, isTrue);
    });

    test('Repeated input deletion', () async {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           scope: p2
           input:
            - p1
            - p2 -> p3+

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
      ''';
      final Recipe recipe = parseRecipe(recipeText);
      final Handler handler = new Handler.fromManifestMatcher(
          new DummyManifestMatcher(),
          runnerFactory: _dummyRunnerFactory);
      final Session session = await handler.createSession(recipe);
      final MutationHelper graph = new MutationHelper(session.graph);

      /* final Edge e1 = */ graph.addEdge(session.root, l1, null, id: 'e1');
      final Edge e2 = graph.addEdge(session.root, l2, null, id: 'e2');
      /* final Edge e3 = */ graph.addEdge(e2.target, l3, null, id: 'e3');
      final Edge e4 = graph.addEdge(session.root, l2, null, id: 'e4');
      final Edge e5 = graph.addEdge(e4.target, l3, null, id: 'e5');
      final Edge e6 = graph.addEdge(e4.target, l3, null, id: 'e6');

      session.start();
      expect(session.modules.length, equals(1));

      expect(session.modules[0].instances.length, equals(2));
      expect(session.modules[0].instances[0], isNotNull);
      expect(session.modules[0].instances[1], isNotNull);

      graph.removeEdge(e6);

      // Nothing should have changed, as this is a repeated match.
      expect(session.modules[0].instances.length, equals(2));
      expect(session.modules[0].instances[0], isNotNull);
      expect(session.modules[0].instances[1], isNotNull);

      graph.removeEdge(e5);

      expect(session.modules[0].instances.length, equals(2));
      expect(session.modules[0].instances[0], isNotNull);
      expect(session.modules[0].instances[1], isNull);
    });
  });
}
