// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:typed_data';

import 'package:handler/module_instance.dart';
import 'package:handler/graph/impl/fake_ledger_graph_store.dart';
import 'package:handler/graph/session_graph.dart';
import 'package:handler/graph/session_graph_store.dart';
import 'package:modular_core/graph/graph.dart';
import 'package:modular/graph/mojo/remote_async_graph.dart';
import 'package:modular_core/graph/query/query.dart';
import 'package:modular_core/graph/query/query_match_set.dart';
import 'package:modular_core/graph/ref.dart';
import 'package:modular_core/graph/test_utils/mutation_helper.dart';
import 'package:modular_core/entity/schema.dart' as entity;
import 'package:modular/modular/graph.mojom.dart' as mojo;
import 'package:modular/modular/module.mojom.dart' as module;
import 'package:modular_core/uuid.dart';
import 'package:parser/manifest.dart';
import 'package:parser/parser.dart';
import 'package:parser/recipe.dart';
import 'package:test/test.dart';

import 'test_session_runner.dart';

// TODO(https://github.com/domokit/modular/issues/617): make less verbose.
Edge findEdgeAddedWithLabel(GraphEvent event, String label) {
  final Iterable<GraphMutation> matchingMutations = event.mutations.where(
      (GraphMutation m) =>
          m.type == GraphMutationType.addEdge && m.labels.contains(label));
  if (matchingMutations.isNotEmpty) {
    return event.graph.edge(matchingMutations.first.edgeId);
  }
  return null;
}

/// [FakeModule] registers incoming calls for later checking.
class FakeModule extends module.Module {
  final Map<String, String> _labelMap = <String, String>{};
  final Completer<Map<String, String>> _completerLabels =
      new Completer<Map<String, String>>();
  final Completer<List<entity.Schema>> _completerSchemas =
      new Completer<List<entity.Schema>>();
  final bool _writeOutput;
  RemoteAsyncGraph _sessionGraph;
  String displayNodeId;

  final Completer<Null> onInitializeComplete = new Completer<Null>();

  FakeModule(this._writeOutput);

  Future<Map<String, String>> getLabels() {
    return _completerLabels.future;
  }

  Future<List<entity.Schema>> getSchemas() {
    return _completerSchemas.future;
  }

  void cleanUp() {
    // Call close() on the RemoteAsyncGraph to avoid leaking a
    // GraphObserverProxy handle leak.
    if (_sessionGraph != null) _sessionGraph.close();
  }

  @override
  Future<Null> onInitialize(final mojo.GraphInterface graph,
      final Map<String, String> types, final List<String> jsonSchemas) async {
    _completerLabels.complete(types);
    _completerSchemas.complete(jsonSchemas
        .map((final String json) => new entity.Schema.fromJsonString(json))
        .toList());
    _labelMap.addAll(types);

    if (!_writeOutput) return;

    _sessionGraph = new RemoteAsyncGraph(graph as mojo.GraphProxy);
    await _sessionGraph.waitUntilReady();

    assert(_labelMap['color'] != null);
    final GraphQueryMatchSet bgMatches =
        _sessionGraph.query(new GraphQuery([_labelMap['color']]));
    assert(bgMatches.length == 1);
    final Node bgNode = bgMatches.single.matchedNodes.single;

    await _sessionGraph.mutateAsync((final GraphMutator gm) {
      final Node outColorNode = gm.addNode();
      final Node cardNode = gm.addNode();

      // Write the output color.
      gm.addEdge(_sessionGraph.nodes.first.id,
          [_labelMap['out-color'], _labelMap['color']], outColorNode.id);
      gm.setValue(
          outColorNode.id, _labelMap['rgb'], bgNode.getValue(_labelMap['rgb']));

      // Write the display edge.
      gm.addEdge(
          _sessionGraph.nodes.first.id, [_labelMap['card']], cardNode.id);

      displayNodeId = cardNode.id.toString();
    });

    onInitializeComplete.complete();
  }
}

void testModuleRunner() {
  final Uri fakeModuleUri = new Uri.http('module.tq.io', 'fakeModule');

  SessionGraph graph;
  Node rootNode;
  FakeModule fakeModule;

  setUp(() async {
    final SessionGraphStore store =
        new SessionGraphStore(new FakeLedgerGraphStore());
    graph = await store.createGraph(new Uuid.random());
    rootNode = graph.root;
  });

  tearDown(() {
    assert(fakeModule != null);
    fakeModule.cleanUp();
  });

  group('ModuleRunner', () {
    test('Initialize a module', () async {
      final Uri backgroundColor =
          new Uri.http('type.tq.io', 'background-color');
      final Uri rgb = new Uri.http('type.tq.io', 'rgb');

      final String manifestYaml = '''
        url: $fakeModuleUri
        verb: verb1

        output:
         - (out-color color) <rgb>

        display:
         - card

        schema:
          - type: http://schema.tq.io/myThing
            properties:
              - name: foo
                type: int

        use:
         - card: http://type.tq.io/card
         - color: http://tq.io/color
         - out-color: http://type.tq.io/out-color
         - rgb: $rgb
         - verb1: http://tq.io/verb1
      ''';

      // TODO(thatguy): The ModuleRunner test should not be referencing
      // Sessions or Recipes at all.
      final String recipeYaml = '''
        recipe:
         - verb: verb1
           output: (background-color out-color color)

        use:
         - background-color: $backgroundColor
         - out-color: http://type.tq.io/out-color
         - color: http://tq.io/color
         - verb1: http://tq.io/verb1
      ''';

      final Manifest testManifest = parseManifest(manifestYaml);
      final Recipe testRecipe = parseRecipe(recipeYaml);
      fakeModule = new FakeModule(false /* writeOutput */);

      final TestSessionRunner sessionRunner = new TestSessionRunner(testRecipe,
          graph, {fakeModuleUri: fakeModule}, <Manifest>[testManifest]);
      sessionRunner.start();

      Map<String, String> actualTypes = await fakeModule.getLabels();
      expect(actualTypes.length, equals(4));
      expect(
          actualTypes,
          allOf([
            contains("color"),
            contains("out-color"),
            contains("rgb"),
            contains("card"),
          ]));

      List<entity.Schema> schemas = await fakeModule.getSchemas();
      // Only check that we got the Schema at all - Schema parsing correctness
      // is tested elsewhere.
      expect(schemas.length, equals(1));
      expect(schemas[0].type, equals('http://schema.tq.io/myThing'));
      sessionRunner.close();
    });

    /// Writes to the graph a color that can be used as input to the Module.
    /// This test then verifies that the module is able to
    /// read its inputs and create output, and copy the
    /// representation value from its input to its output. Also deletes the
    /// input edge and verifies that the module instance is torn down.
    test('Module I/O, writes output and deletes edges', () async {
      final String manifestYaml = '''
        verb: verb1
        url: $fakeModuleUri

        input:
         - color <rgb>

        output:
         - (out-color color) <rgb>

        display:
         - card

        use:
         - card: http://type.tq.io/card
         - color: http://type.tq.io/color
         - out-color: http://type.tq.io/out-color
         - rgb: http://type.tq.io/rgb
         - verb1: http://verb.tq.io/verb1
      ''';
      final String recipeYaml = '''
        recipe:
         - verb: verb1
           input:
            - (background-color color)
           output:
            - (foreground-color out-color color)

        use:
         - background-color: http://type.tq.io/background-color
         - foreground-color: http://type.tq.io/foreground-color
         - color: http://type.tq.io/color
         - out-color: http://type.tq.io/out-color
         - rgb: http://type.tq.io/rgb
         - verb1: http://verb.tq.io/verb1
      ''';
      final Manifest testManifest = parseManifest(manifestYaml);
      final Recipe testRecipe = parseRecipe(recipeYaml);
      fakeModule = new FakeModule(true);

      final String bgLabel = 'http://type.tq.io/background-color';
      final String colorLabel = 'http://type.tq.io/color';

      // We explicitly set the Module's input before starting it, to show that
      // the Module is notified of updated inputs by virtue of its inputs
      // already existing.
      Edge bgColorEdge;
      graph.mutate((GraphMutator mutator) {
        bgColorEdge = mutator.addEdge(rootNode.id, [bgLabel, colorLabel]);
        mutator.setValue(bgColorEdge.target.id, 'http://type.tq.io/rgb',
            new Uint8List.fromList(<int>[255, 0, 0]));
      });

      final Completer<Edge> outputEdgeCompleter = new Completer<Edge>();
      graph.addObserver((final GraphEvent event) {
        // Wait for our expected output edge to appear.
        final Edge edge =
            findEdgeAddedWithLabel(event, 'http://type.tq.io/foreground-color');
        if (!outputEdgeCompleter.isCompleted && edge != null) {
          outputEdgeCompleter.complete(edge);
        }
      });

      bool closeCalled = false;
      final TestSessionRunner sessionRunner = new TestSessionRunner(
          testRecipe,
          graph,
          {fakeModuleUri: fakeModule},
          <Manifest>[testManifest], closeProxyCallback:
              (final ModuleInstance instance, final module.ModuleProxy proxy) {
        closeCalled = true;
        // This callback is executed outside of the test, so we can't call
        // expect.
        assert(instance.manifest.url == fakeModuleUri);
      });
      sessionRunner.start();

      // Wait until we've seen a Graph update.
      final Edge outputEdge = await outputEdgeCompleter.future;

      expect(outputEdge.origin, equals(graph.root));
      expect(outputEdge.labels.first,
          equals('http://type.tq.io/foreground-color'));
      final Node outputNode = outputEdge.target;
      expect(outputNode.getValue('http://type.tq.io/rgb'), <int>[255, 0, 0]);

      // Delete the edge and check that close is called for the previously
      // created module.
      graph.mutate((GraphMutator mutator) {
        mutator.removeEdge(bgColorEdge.id);
      });

      expect(closeCalled, isTrue);
      sessionRunner.close();
    });

    test('Compose module callback with display node id', () async {
      final String manifestYaml = '''
        verb: verb1
        url: $fakeModuleUri

        input:
         - color <rgb>

        output:
         - (out-color color) <rgb>

        display:
         - card

        use:
         - card: http://type.tq.io/card
         - color: http://type.tq.io/color
         - out-color: http://type.tq.io/out-color
         - rgb: http://type.tq.io/rgb
         - verb1: http://verb.tq.io/verb1
      ''';

      final String recipeYaml = '''
        recipe:
         - verb: verb1
           input:
            - (background-color color)
           output:
            - (foreground-color out-color color)
           display:
            - card

        use:
         - background-color: http://type.tq.io/background-color
         - foreground-color: http://type.tq.io/foreground-color
         - card: http://type.tq.io/card
         - color: http://type.tq.io/color
         - out-color: http://type.tq.io/out-color
         - rgb: http://type.tq.io/rgb
         - verb1: http://verb.tq.io/verb1
      ''';

      final Manifest testManifest = parseManifest(manifestYaml);
      final Recipe testRecipe = parseRecipe(recipeYaml);
      fakeModule = new FakeModule(true);

      final Completer<String> displayEdgeCompleter = new Completer<String>();
      final TestSessionRunner sessionRunner = new TestSessionRunner(
          testRecipe,
          graph,
          {fakeModuleUri: fakeModule},
          <Manifest>[testManifest], composeModuleCallback:
              (ModuleInstance instance, String displayNodeId) {
        if (displayNodeId != null && !displayEdgeCompleter.isCompleted) {
          displayEdgeCompleter.complete(displayNodeId);
        }
      });

      final MutationHelper helper = new MutationHelper(graph);
      final String bgLabel = 'http://type.tq.io/background-color';
      final String colorLabel = 'http://type.tq.io/color';

      final Node bgNode = helper.addNode();
      final String rgbLabel = 'http://type.tq.io/rgb';
      helper.setValue(
          bgNode, rgbLabel, new Uint8List.fromList(<int>[255, 0, 0]));
      helper.addEdge(graph.root, [bgLabel, colorLabel], bgNode);

      sessionRunner.start();

      await fakeModule.onInitializeComplete.future;
      expect(
          await displayEdgeCompleter.future, equals(fakeModule.displayNodeId));
      sessionRunner.close();
    });
  });
}
