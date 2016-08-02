// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:handler/graph/impl/fake_ledger_graph_store.dart';
import 'package:handler/graph/session_graph.dart';
import 'package:handler/graph/session_graph_store.dart';
import 'package:handler/handler.dart';
import 'package:handler/module_runner.dart';
import 'package:handler/session.dart' show Session;
import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/uuid.dart';
import 'package:parser/expression.dart';
import 'package:parser/manifest.dart';
import 'package:parser/parser.dart';
import 'package:parser/recipe.dart';
import 'package:test/test.dart';

import '../test_util/recording_module_runner.dart';

void main() {
  group('Session', () {
    test('Creation', () async {
      final String recipeText = '''
        title: t

        recipe:
         - verb: verb1
           input:
            - input1
            - input2 -> input3

        use:
         - input1: http://tq.io/input1
         - input2: http://tq.io/input2
         - input3: http://tq.io/input3
         - verb1: http://tq.io/verb1
      ''';
      final Recipe recipe = parseRecipe(recipeText);
      final SessionGraphStore store =
          new SessionGraphStore(new FakeLedgerGraphStore());
      final SessionGraph graph = await store.createGraph(new Uuid.random());
      final Session session =
          new Session.fromRecipe(recipe: recipe, graph: graph);
      expect(session, isNotNull);
    });

    test('Add and remove steps', () async {
      final Uri verb1 = new Uri.http('tq.io', 'v1');
      final Uri type1 = new Uri.http('tq.io', 'p1');
      final List<String> p1 = [type1.toString()];
      final Property property1 = new Property([new Label.fromUri(type1)]);
      final Uri testUrl = new Uri.http("tq.io", "u1");
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input: p2

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
      ''';
      final String manifestText = '''
        verb: v1
        input: p1
        url: $testUrl

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
      ''';
      final String manifest2Text = '''
        verb: v1
        input: p2

        use:
         - v1: http://tq.io/v1
         - p2: http://tq.io/p2
      ''';

      final Recipe recipe = parseRecipe(recipeText);
      final RecordingModuleRunner runner = new RecordingModuleRunner();
      final ModuleRunnerFactory runnerFactory = () => runner;
      final Manifest manifest = parseManifest(manifestText);
      final Manifest manifest2 = parseManifest(manifest2Text);
      final Handler handler = new Handler(
          manifests: [manifest, manifest2], runnerFactory: runnerFactory);
      final Session session = await handler.createSession(recipe);
      final Graph graph = session.graph;

      session.start();

      expect(session.recipe.steps.length, equals(1));
      expect(session.modules.length, equals(1));
      expect(session.modules[0].manifest.url, isNull);

      final Step newStep = new Step(null, new Verb(new Label.fromUri(verb1)),
          [new PathExpr.single(property1)], [], [], [], null);
      session.update(addSteps: [newStep]);
      expect(session.recipe.steps.length, equals(2));
      expect(session.modules.length, equals(2));
      expect(session.modules[1].manifest.url, equals(testUrl));
      expect(runner.instance, isNull);

      graph.mutate((GraphMutator mutator) {
        mutator.addEdge(session.root.id, p1);
      });

      expect(runner.instance, isNotNull);
      // The module should have been updated, as a new p1 edge has been added to
      // the graph.
      expect(runner.updateCallCount, equals(1));

      // Add the same step again.
      session.update(addSteps: [newStep]);
      expect(session.recipe.steps.length, equals(2));
      expect(session.modules.length, equals(2));
      expect(runner.updateCallCount, equals(1));

      expect(runner.running, isTrue);
      // Remove step.
      session.update(removeSteps: [newStep]);
      expect(session.recipe.steps.length, equals(1));
      expect(session.modules.length, equals(1));
      expect(runner.running, isFalse);

      // Check that new edges to p1 doesn't create new instances anymore.
      graph.mutate((GraphMutator mutator) {
        mutator.addEdge(session.root.id, p1);
      });

      // Check that update is not called again.
      expect(runner.updateCallCount, equals(1));
    });

    test('Swap steps', () async {
      final Uri verb1 = new Uri.http('tq.io', 'v1');
      final Uri type1 = new Uri.http('tq.io', 'p1');
      final List<String> p1 = [type1.toString()];
      final Property property1 = new Property([new Label.fromUri(type1)]);
      final Uri test1Url = new Uri.http("tq.io", "u1");
      final Uri test2Url = new Uri.http("tq.io", "u2");
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v0
           input: p0
         - verb: v1
           input: p2

        use:
         - v0: http://tq.io/v0
         - v1: http://tq.io/v1
         - p0: http://tq.io/p0
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
      ''';
      final String manifest0Text = '''
        verb: v0
        input: p0

        use:
         - v0: http://tq.io/v0
         - p0: http://tq.io/p0
      ''';
      final String manifest1Text = '''
        verb: v1
        input: p1
        url: $test1Url

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
      ''';
      final String manifest2Text = '''
        verb: v1
        input: p2
        url: $test2Url

        use:
         - v1: http://tq.io/v1
         - p2: http://tq.io/p2
      ''';

      final Recipe recipe = parseRecipe(recipeText);
      final RecordingModuleRunner runner = new RecordingModuleRunner();
      final ModuleRunnerFactory runnerFactory = () => runner;
      final Manifest manifest0 = parseManifest(manifest0Text);
      final Manifest manifest1 = parseManifest(manifest1Text);
      final Manifest manifest2 = parseManifest(manifest2Text);
      final Handler handler = new Handler(
          manifests: [manifest0, manifest1, manifest2],
          runnerFactory: runnerFactory);
      final Session session = await handler.createSession(recipe);
      final Graph graph = session.graph;

      session.start();

      expect(session.recipe.steps.length, equals(2));
      expect(session.modules.length, equals(2));
      expect(session.modules[0].manifest.url, isNull);
      expect(session.modules[1].manifest.url, equals(test2Url));

      // Insert an edge for p1 into the graph, so that the add step below
      // instantiates the modules.
      graph.mutate((GraphMutator mutator) {
        mutator.addEdge(session.root.id, p1);
      });

      final Step newStep = new Step(null, new Verb(new Label.fromUri(verb1)),
          [new PathExpr.single(property1)], [], [], [], null);
      // Swap step with the existing step.
      session
          .update(addSteps: [newStep], removeSteps: [session.recipe.steps[1]]);
      expect(session.recipe.steps.length, equals(2));
      expect(session.modules.length, equals(2));
      expect(session.modules[1].manifest.url, equals(test1Url));
      expect(runner.instance, isNotNull);

      expect(runner.updateCallCount, equals(1));
    });

    test('Update steps on recipe change', () async {
      final Uri type1 = new Uri.http('tq.io', 'p1');
      final List<String> p1 = [type1.toString()];
      final Uri test1Url = new Uri.http("tq.io", "u1");
      final Uri test2Url = new Uri.http("tq.io", "u2");
      final String initialRecipeText = '''
        title: foo

        recipe:
         - verb: v0
           input: p0
         - verb: v1
           input: p2

        use:
         - v0: http://tq.io/v0
         - v1: http://tq.io/v1
         - p0: http://tq.io/p0
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
      ''';
      final String updateRecipeText = '''
        title: foo

        recipe:
         - verb: v0
           input: p0
         - verb: v1
           input: p1

        use:
         - v0: http://tq.io/v0
         - v1: http://tq.io/v1
         - p0: http://tq.io/p0
         - p1: http://tq.io/p1
      ''';
      final String manifest0Text = '''
        verb: v0
        input: p0

        use:
         - v0: http://tq.io/v0
         - p0: http://tq.io/p0
      ''';
      final String manifest1Text = '''
        verb: v1
        input: p1
        url: $test1Url

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
      ''';
      final String manifest2Text = '''
        verb: v1
        input: p2
        url: $test2Url

        use:
         - v1: http://tq.io/v1
         - p2: http://tq.io/p2
      ''';

      final Recipe initialRecipe = parseRecipe(initialRecipeText);
      final Recipe updateRecipe = parseRecipe(updateRecipeText);
      final RecordingModuleRunner runner = new RecordingModuleRunner();
      final ModuleRunnerFactory runnerFactory = () => runner;
      final Manifest manifest0 = parseManifest(manifest0Text);
      final Manifest manifest1 = parseManifest(manifest1Text);
      final Manifest manifest2 = parseManifest(manifest2Text);
      final Handler handler = new Handler(
          manifests: [manifest0, manifest1, manifest2],
          runnerFactory: runnerFactory);
      final Session session = await handler.createSession(initialRecipe);
      final Graph graph = session.graph;

      session.start();

      expect(session.recipe.steps.length, equals(2));
      expect(session.modules.length, equals(2));
      expect(session.modules[0].manifest.url, isNull);
      expect(session.modules[1].manifest.url, equals(test2Url));

      // Insert an edge for p1 into the graph, so that the add step below
      // instantiates the modules.
      graph.mutate((GraphMutator mutator) {
        mutator.addEdge(session.root.id, p1);
      });

      // Update recipe, which swaps a step in the recipe.
      session.metadata.setRecipe(updateRecipe);

      expect(session.recipe.steps.length, equals(2));
      expect(session.modules.length, equals(2));
      expect(session.modules[1].manifest.url, equals(test1Url));
      expect(runner.instance, isNotNull);

      expect(runner.updateCallCount, equals(1));
    });

    test('Start and stop session', () async {
      final Uri type1 = new Uri.http('tq.io', 'p1');
      final List<String> p1 = [type1.toString()];
      final Uri testUrl = new Uri.http("tq.io", "u1");
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
        input: p1
        url: $testUrl

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
      final Graph graph = session.graph;

      int startCbCount = 0;
      int stopCbCount = 0;
      Session lastSession;
      handler.addSessionObserver((final Session session) {
        startCbCount++;
        lastSession = session;
      }, (final Session session) {
        stopCbCount++;
        lastSession = session;
      });

      graph.mutate((GraphMutator mutator) {
        mutator.addEdge(session.root.id, p1);
      });

      session.start();

      expect(session.recipe.steps.length, equals(1));
      expect(session.modules.length, equals(1));
      expect(session.modules[0].manifest.url, equals(testUrl));
      expect(runner.instance, isNotNull);

      expect(startCbCount, equals(1));
      expect(lastSession, equals(session));

      // Check that instance is running.
      expect(runner.running, isTrue);

      lastSession = null;

      session.stop();

      // Check that instance is stopped.
      expect(runner.running, isFalse);

      expect(stopCbCount, equals(1));
      expect(lastSession, equals(session));
    });
  });
}
