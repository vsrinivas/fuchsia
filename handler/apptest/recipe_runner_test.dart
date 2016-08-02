// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:handler/graph/impl/fake_ledger_graph_store.dart';
import 'package:handler/graph/session_graph.dart';
import 'package:handler/graph/session_graph_store.dart';
import 'package:modular_core/graph/graph.dart';
import 'package:modular/graph/mojo/remote_async_graph.dart';
import 'package:modular/modular/graph.mojom.dart' as mojo;
import 'package:modular/modular/module.mojom.dart' as module;
import 'package:modular/state_graph.dart';
import 'package:modular_core/uuid.dart';
import 'package:parser/expression.dart';
import 'package:parser/manifest.dart';
import 'package:parser/parser.dart';
import 'package:parser/recipe.dart';
import 'package:test/test.dart';

import 'test_session_runner.dart';

// We track the created graphs here so that we can clean them up to avoid
// leaking mojo handles.
final List<RemoteAsyncGraph> footprintGraphs = [];

Future<StateGraph> _initializeStateGraph(final Object graphObj,
    final Map<String, String> labelMap, final String debugTag,
    [final Function onChange]) async {
  final RemoteAsyncGraph graph =
      new RemoteAsyncGraph(graphObj as mojo.GraphProxy);
  footprintGraphs.add(graph);
  graph.metadata.debugName = 'Footprint graph: $debugTag';
  graph.addObserver((final GraphEvent event) {
    print('Footprint Graph changed ($debugTag): ${event.mutations}');
  });
  if (onChange != null) {
    graph.addObserver((_) => onChange());
  }
  await graph.waitUntilReady();
  return new StateGraph(graph, labelMap);
}

/// [OutputManyEdgesModule] takes no input and outputs [_outputCount] edges,
/// commits them, then deletes [_deleteCount] edges.
class OutputManyEdgesModule extends module.Module {
  final Map<String, String> _labelMap = <String, String>{};
  final int _outputCount;
  final int _deleteCount;

  OutputManyEdgesModule(this._outputCount, this._deleteCount);

  @override
  Future<Null> onInitialize(final mojo.GraphInterface graph,
      final Map<String, String> types, final List<String> jsonSchemas) async {
    _labelMap.addAll(types);
    final StateGraph state =
        await _initializeStateGraph(graph, _labelMap, '$runtimeType');

    // Let's create some edges.
    for (int i = 0; i < this._outputCount; ++i) {
      state.addEdge(state.root, state.addNode(),
          [Uri.parse(_labelMap["background-color"])]);
    }

    await state.push();

    final List<SemanticNode> nodes =
        state.getNeighbors([Uri.parse(_labelMap["background-color"])]).toList();
    for (int i = 0; i < this._deleteCount; i++) {
      state.root.delete([_labelMap["background-color"]], nodes[i]);
    }

    await state.push();
  }
}

/// [InputCounterModule] ignores [ignoredOnChangeCount] calls to its onChange
/// method, then completes [_inputCount] with the number of input edges found.
class InputCounterModule extends module.Module {
  final Map<String, String> _labelMap = <String, String>{};
  final Completer<int> _inputCount = new Completer<int>();
  StateGraph _state;

  @override
  Future<Null> onInitialize(final mojo.GraphInterface graph,
      final Map<String, String> types, final List<String> jsonSchemas) async {
    _labelMap.addAll(types);
    _state = await _initializeStateGraph(
        graph, _labelMap, '$runtimeType', _onChange);
  }

  void _onChange() {
    final Iterable<SemanticNode> nodesIterable =
        _state.getNeighbors([Uri.parse(_labelMap["background-color"])]);
    _inputCount.complete(nodesIterable.length);
  }

  Future<int> get inputCount => _inputCount.future;
}

/// [SameInputOutputCounterModule] reads and outputs the same label. The module
/// updates [_inputCount] on every onChange with the number of input edges
/// found.
class SameInputOutputCounterModule extends module.Module {
  final Map<String, String> _labelMap = <String, String>{};
  int _inputCount = 0;
  bool _outputDone = false;
  int _currentOnChange = 0;
  StateGraph _state;

  final Completer<int> _onChangeCount = new Completer<int>();

  @override
  Future<Null> onInitialize(final mojo.GraphInterface graph,
      final Map<String, String> types, final List<String> jsonSchemas) async {
    _labelMap.addAll(types);
    _state = await _initializeStateGraph(
        graph, _labelMap, '$runtimeType', _onChange);
    _onChange();
  }

  Future<Null> _onChange() async {
    _inputCount =
        _state.getNeighbors([Uri.parse(_labelMap["background-color"])]).length;

    if (!_outputDone) {
      _outputDone = true;
      _state.addEdge(_state.root, _state.addNode(),
          [Uri.parse(_labelMap["background-color"])]);
    }

    await _state.push();

    ++_currentOnChange;
    if (_currentOnChange == 2) {
      _onChangeCount.complete();
    }
  }

  Future<int> get onChangeCount => _onChangeCount.future;
  int get inputCount => _inputCount;
}

/// [OutputOnInputModule] reads an edge "p1" and outputs an edge "p2" attached
/// to "p1".
class OutputOnInputModule extends module.Module {
  final Map<String, String> _labelMap = <String, String>{};

  @override
  Future<Null> onInitialize(final mojo.GraphInterface graph,
      final Map<String, String> types, final List<String> jsonSchemas) async {
    _labelMap.addAll(types);
    final StateGraph state =
        await _initializeStateGraph(graph, _labelMap, '$runtimeType');

    final SemanticNode n1 = state.root.get(["p1"]);
    assert(n1 != null);
    n1.getOrDefault(["p2"]);

    await state.push();
  }
}

/// Tests full runs of a recipe, with multiple modules exchanging data.
void testRecipeRuns() {
  final Uri moduleUri = Uri.parse('https://tq.io/module');
  final Uri noInputModuleUri = Uri.parse('https://module.tq.io/no-input');
  final Uri inputCounterModuleUri =
      Uri.parse('https://module.tq.io/input-counter');

  final Uri backgroundColor = Uri.parse('https://tq.io/background-color');
  final Label p1 = new Label.fromUri(Uri.parse('https://tq.io/p1'));
  final Label p2 = new Label.fromUri(Uri.parse('https://tq.io/p2'));

  SessionGraph graph;

  setUp(() async {
    final SessionGraphStore store =
        new SessionGraphStore(new FakeLedgerGraphStore());
    graph = await store.createGraph(new Uuid.random());
  });

  tearDown(() {
    for (final RemoteAsyncGraph graph in footprintGraphs) {
      graph.close();
    }
  });

  group('Recipe runs', () {
    test('2 modules, 2 edges, repeated', () async {
      final String manifest1Yaml = '''
        verb: verb1
        url: $noInputModuleUri
        output: background-color

        use:
         - background-color: $backgroundColor
         - verb1: http://tq.io/verb1
      ''';
      final String manifest2Yaml = '''
        verb: verb2
        url: $inputCounterModuleUri
        input: background-color+

        use:
         - background-color: $backgroundColor
         - verb2: http://tq.io/verb2
      ''';
      final String recipeYaml = '''
        recipe:
         - verb: verb1
           output: background-color

         - verb: verb2
           input: background-color+

        use:
         - background-color: $backgroundColor
         - verb1: http://tq.io/verb1
         - verb2: http://tq.io/verb2
      ''';
      final Manifest manifest1 = parseManifest(manifest1Yaml);
      final Manifest manifest2 = parseManifest(manifest2Yaml);
      final Recipe testRecipe = parseRecipe(recipeYaml);

      final int numEdges = 5;
      final int numDeletedEdges = 3;

      final InputCounterModule inputCounterModule = new InputCounterModule();
      final Map<Uri, module.Module> moduleMap = {
        noInputModuleUri: new OutputManyEdgesModule(numEdges, numDeletedEdges),
        inputCounterModuleUri: inputCounterModule
      };

      final TestSessionRunner sessionRunner = new TestSessionRunner(
          testRecipe, graph, moduleMap, <Manifest>[manifest1, manifest2]);

      sessionRunner.start();

      expect(await inputCounterModule.inputCount,
          equals(numEdges - numDeletedEdges));

      sessionRunner.close();
    });

    test('1 module, 1 edge, not duplicate edges', () async {
      final String manifest1Yaml = '''
        verb: verb1
        url: $inputCounterModuleUri
        input: background-color?
        output: background-color

        use:
         - background-color: $backgroundColor
         - verb1: http://tq.io/verb1
      ''';
      final String recipeYaml = '''
        recipe:
         - verb: verb1
           input: background-color?
           output: background-color

        use:
         - background-color: $backgroundColor
         - verb1: http://tq.io/verb1
      ''';
      final Manifest manifest1 = parseManifest(manifest1Yaml);
      final Recipe testRecipe = parseRecipe(recipeYaml);

      final SameInputOutputCounterModule counterModule =
          new SameInputOutputCounterModule();
      final Map<Uri, module.Module> moduleMap = {
        inputCounterModuleUri: counterModule
      };

      final Completer<Null> firstUpdate = new Completer<Null>();
      final TestSessionRunner sessionRunner = new TestSessionRunner(
          testRecipe, graph, moduleMap, <Manifest>[manifest1]);

      graph.addObserver((final GraphEvent event) {
        // TODO(https://github.com/domokit/modular/issues/617): make less
        // verbose.
        final bool moduleMutationsExists = event.mutations.any(
            (GraphMutation m) =>
                (m.type == GraphMutationType.addEdge ||
                    m.type == GraphMutationType.removeEdge) &&
                m.labels.contains('$backgroundColor'));
        if (!moduleMutationsExists) {
          return;
        }

        assert(!firstUpdate.isCompleted);
        firstUpdate.complete();
      });
      sessionRunner.start();

      await firstUpdate.future;

      // The first update should have inputCount 0;
      expect(counterModule.inputCount, 0);

      await counterModule.onChangeCount;

      // The second update should have inputCount 1;
      expect(counterModule.inputCount, 1);

      // There should be no second onChange call.
      sessionRunner.close();
    });

    test('Scoped output edge', () async {
      final String manifest1Yaml = '''
        verb: verb1
        url: $moduleUri
        input: p1
        output: p2

        use:
         - p1: ${p1.uri}
         - p2: ${p2.uri}
         - verb1: http://tq.io/verb1
      ''';
      final String recipeYaml = '''
        recipe:
         - verb: verb1
           input: p1
           output: p1 -> p2

        use:
         - p1: ${p1.uri}
         - p2: ${p2.uri}
         - verb1: http://tq.io/verb1
      ''';
      final Manifest manifest1 = parseManifest(manifest1Yaml);
      final Recipe testRecipe = parseRecipe(recipeYaml);

      final OutputOnInputModule moduleImpl = new OutputOnInputModule();
      final Map<Uri, module.Module> moduleMap = {moduleUri: moduleImpl};

      final Completer<Null> firstUpdate = new Completer<Null>();
      final TestSessionRunner sessionRunner = new TestSessionRunner(
          testRecipe, graph, moduleMap, <Manifest>[manifest1]);

      final Node rootNode = graph.root;
      Edge e1;
      graph.mutate((final GraphMutator mutator) {
        e1 = mutator.addEdge(rootNode.id, [p1.toString()]);
      });
      graph.addObserver((final GraphEvent event) {
        // TODO(https://github.com/domokit/modular/issues/617): make less
        // verbose.
        final Iterable<GraphMutation> moduleMutations = event.mutations.where(
            (GraphMutation m) =>
                (m.type == GraphMutationType.addEdge ||
                    m.type == GraphMutationType.removeEdge) &&
                m.labels.contains('${p2.uri}'));
        if (moduleMutations.isEmpty) {
          return;
        }
        firstUpdate.complete();
        assert(moduleMutations.length == 1);
      });

      sessionRunner.start();
      await firstUpdate.future;

      // Make sure that the output edge was created from the target of [e1].
      final Node n1 = e1.target;
      expect(n1.outEdges.first.labels, contains(p2.toString()));

      sessionRunner.close();
    });
  });
}
