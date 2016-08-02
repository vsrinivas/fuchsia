// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:common/incrementer.dart';
import 'package:modular_core/graph/algorithm.dart' show pathVisit;
import 'package:modular_core/graph/graph.dart';
import 'package:parser/expression.dart' show PathExpr;
import 'package:parser/recipe.dart' show Step;
import 'package:parser/manifest.dart' show Manifest;

import 'inspector_json_server.dart';
import 'errors.dart';
import 'module_instance.dart';
import 'session_match.dart';
import 'session_pattern.dart';

/// A data structure that represents an atomic update to the graph to apply to
/// session matches. It consists of the edges to traverse, the IDs of edges
/// deleted, and the nodes whose values were updated. Instances of this
/// structure are propagated through the module machinery to update matches and
/// notify modules of changes to their footprint.
class SessionMatchUpdate {
  /// Uses [graph] to resolve node and edge ID references in [mutations].
  SessionMatchUpdate.fromGraphMutations(
      final Graph graph, final Iterable<GraphMutation> mutations) {
    modifiedNodes.addAll(mutations
        .where((final GraphMutation mutation) =>
            mutation.type == GraphMutationType.setValue)
        .map((final GraphMutation mutation) => graph.node(mutation.nodeId)));

    deletedEdgeIds.addAll(mutations
        .where((final GraphMutation mutation) =>
            mutation.type == GraphMutationType.removeEdge)
        .map((final GraphMutation mutation) => mutation.edgeId));

    // Holds all the new edges to the graph in path traversal order.
    final Iterable<Edge> addedEdges = mutations
        .where((final GraphMutation mutation) =>
            mutation.type == GraphMutationType.addEdge)
        .map((final GraphMutation mutation) => graph.edge(mutation.edgeId));

    addedEdges.forEach((final Edge addedEdge) {
      updatedEdges.add(addedEdge);
      // Add all edges which form new paths possible with the new added edge.
      // Lets say the graph is  A -> B -> C  (Where A, B, C, D are nodes).
      //                        |
      //                        D
      // Now if a new edge is added between D and B nodes then we should send
      // path D -> B -> C
      pathVisit(graph, addedEdge.target, (final Edge edge) {
        // Note that [updatedEdges] could have duplicate edges, as the
        // newly added edge could appear in more than one possible paths in
        // the graph.
        updatedEdges.add(edge);
      });
    });

    // Holds all the edges in the graph in path traversal order. These list of
    // edges are needed for initializng matches for newly created step and
    // instances.
    graph.nodes
        .where((final Node node) => node.inEdges.isEmpty)
        .forEach((final Node root) {
      pathVisit(graph, root, (final Edge edge) {
        allEdges.add(edge);
      });
    });
  }

  /// Edges in graph traversal order needed for incremental updates to existing
  /// matches. This list contains all edges added by this update, and all edges
  /// reachable from them.
  final List<Edge> updatedEdges = <Edge>[];

  /// Edges in graph traversal order needed to initialize new session matches.
  /// This list contains all edges in the graph.
  final List<Edge> allEdges = <Edge>[];
  final List<EdgeId> deletedEdgeIds = <EdgeId>[];
  final Set<Node> modifiedNodes = new Set<Node>();

  @override
  String toString() {
    String ret = '$runtimeType:\n';
    ret += 'deleted edges: $deletedEdgeIds\n';
    ret += 'modified nodes: $modifiedNodes\n';
    return ret;
  }
}

/// A Module helps the RecipeRunner to track the state of one Step in the
/// Recipe. It has two separate functions:
///
/// 1. It holds all matches in the Graph of all inputs and outputs of the Step.
///    The class SessionPattern is responsible for this aspect.
///
/// 2. It holds all the module instances that the matches of the inputs require.
///    The class ModuleInstance is responsible for this aspect. There is one
///    ModuleInstance for each match of the scope expression of the recipe step.
///    The RecipeRunner actually creates the instances.
class Module implements Inspectable {
  // The recipe step this module keeps track of.
  final Step step;

  // The resolved module manifest. It is necessary to know the manifest early to
  // be able to match and gate instantiation of module implementation on the
  // representation data, which may be only available in the manifest.
  final Manifest manifest;

  // If the step has a scope expression, this tracks the matches of that
  // expression. If this expression exists, each match on it yields one module
  // instance. If it doesn't exist, there is at most one module instance. See
  // below at |instances| for how matches of inputs and outputs are assigned to
  // module instances.
  final SessionPattern scope;

  // For each input of the step in the recipe, there is one SessionPattern that
  // tracks the places in the Graph that match the input specification from the
  // recipe.
  final List<SessionPattern> inputs = <SessionPattern>[];

  // Tracks the places in the Graph that match the output specification from the
  // recipe. When an instance is created, the currently matching output values
  // are supplied to the instance. This happens when a session is restarted, and
  // also when a step is added to a recipe already running in a session.
  final List<SessionPattern> outputs = <SessionPattern>[];

  // Module instances created for matching inputs. There is one module instance
  // for every match of the scope expression of the recipe step. The matches of
  // all other expressions are assigned to the instances as follows:
  //
  // - Matches of inputs with expressions that share a prefix with the scope
  //   expression are assigned to the same instance as the corresponding match
  //   of the scope expression.
  //
  // - Of all other inputs, the first match is passed to all instances. If there
  //   is more than one match, a warning is logged. The warning can be avoided
  //   by making those inputs repeated, because then they only have at most one
  //   match.
  //
  // - The same rules apply to outputs. However, output expressions are
  //   implicitly considered repeated where they don't overlap with the scope
  //   expression, so they never create a warning.
  //
  // - If a recipe step has no scope expression, there will only be one instance
  //   of it.
  //
  // NOTE(mesch): These rules really only apply to step expressions and prepare
  // to turn inputs and outputs into uniform scope expressions. We will
  // introduce different matching rules for manifest expressions as we turn them
  // into footprint expressions.
  final List<ModuleInstance> instances = <ModuleInstance>[];

  // An ID that allows us to reference the instance from the command line, and a
  // global counter to initialize it from.
  static final Incrementer _incrementer = new Incrementer();
  final int id = _incrementer.next;

  final InspectorJSONServer _inspector;

  Module(final Step step, this.manifest, final Node root, this._inspector)
      : this.step = step,
        this.scope = step.scope != null
            ? new SessionPattern(step.scope, [root], footprint: false)
            : null {
    if (manifest == null) {
      throw new NoMatchedManifestError('No manifest found for step: $step');
    }

    // Create the matchers for the inputs and composes of this module.
    for (final PathExpr expr in <PathExpr>[]
      ..addAll(step.input)
      ..addAll(step.compose)) {
      inputs.add(new SessionPattern(expr, [root], footprint: false));
    }

    // Create the matchers for the outputs and displays of this module.
    //
    // TODO(mesch): Output expressions are implicitly repeated optional, but
    // they are stored as singular. This doesn't matter as long as we don't use
    // the matches for anything, but if we actually add output matches to the
    // anchor matches of the module instances, we have to compute them
    // correctly, and thus convert them to optional repeated here.
    for (final PathExpr expr in <PathExpr>[]
      ..addAll(step.output)
      ..addAll(step.display)) {
      outputs.add(new SessionPattern(expr, [root], footprint: false));
    }

    // Publish this module to the debug server
    _inspector?.publish(this);
  }

  /// Deletes edges specified in the update data. This must be done separately
  /// from adding edges so that module instances can be torn down when an edge
  /// is deleted at the same time another is added that matches the same input.
  void deleteEdges(final SessionMatchUpdate updateData) {
    if (scope != null) {
      scope.deleteEdges(updateData);
    }

    for (final SessionPattern moduleInput in inputs) {
      moduleInput.deleteEdges(updateData);
    }

    for (final SessionPattern moduleOutput in outputs) {
      moduleOutput.deleteEdges(updateData);
    }

    _inspector?.notify(this);
  }

  /// Updates this module matches with the provided edges if they fit the path
  /// expression of any of its inputs and outputs and previously matched edges.
  void updateMatches(final SessionMatchUpdate updateData) {
    if (scope != null) {
      scope.updateMatches(updateData);
    }

    for (final SessionPattern moduleInput in inputs) {
      moduleInput.updateMatches(updateData);
    }

    for (final SessionPattern moduleOutput in outputs) {
      moduleOutput.updateMatches(updateData);
    }

    _inspector?.notify(this);
  }

  /// Clears empty matches, except for the first. Resets the dirty flags.
  void clearMatches() {
    if (scope != null) {
      scope.clearMatches();
    }

    for (final SessionPattern moduleInput in inputs) {
      moduleInput.clearMatches();
    }

    for (final SessionPattern moduleOutput in outputs) {
      moduleOutput.clearMatches();
    }

    _inspector?.notify(this);
  }

  Iterable<ModuleInstance> get activeInstances =>
      instances.where((i) => i != null);

  @override
  String toString() => "$runtimeType:$inputs/$instances";

  @override // Inspectable
  String get inspectorPath => '/module/$id';

  @override // Inspectable
  Future<dynamic> inspectorJSON() async {
    Map<String, dynamic> ioJSON(SessionPattern io) => {
          'pathExpr': io.pathExpr.toString(),
          'anchorSet': io.anchorSet?.map(_inspector.node) ?? [],
          'matches':
              io.matches?.map((SessionMatch sm) => sm.inspectorJSON()) ?? [],
        };

    List<Map<String, dynamic>> instancesJSON = await Future.wait(instances.map(
        (ModuleInstance instance) =>
            (instance?.inspectorJSON() ?? new Future<Null>.value())));
    return {
      'type': 'module',
      'id': id,
      'verb': step.verb.toString(),
      'url': step.url,
      'step': step.toString(),
      'inputs': inputs?.map((SessionPattern io) => ioJSON(io)) ?? [],
      'outputs': outputs?.map((SessionPattern io) => ioJSON(io)) ?? [],
      'inputExprs': step.input.map((PathExpr p) => p.toString()),
      'outputExprs': step.output.map((PathExpr p) => p.toString()),
      'composeExprs': step.compose.map((PathExpr p) => p.toString()),
      'displayExprs': step.display.map((PathExpr p) => p.toString()),
      'instances': instancesJSON,
    };
  }

  @override // Inspectable
  Future<dynamic> onInspectorPost(dynamic json) async {}
}
