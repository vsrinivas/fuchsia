// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:common/incrementer.dart';
import 'package:modular_core/log.dart';
import 'package:modular_core/graph/graph.dart';
import 'package:parser/expression.dart' show PathExpr;
import 'package:parser/manifest.dart' show Manifest;
import 'package:parser/recipe.dart' show Step;

import 'bindings.dart';
import 'inspector_json_server.dart';
import 'module.dart' show SessionMatchUpdate;
import 'module_label_mapper.dart';
import 'module_runner.dart';
import 'session.dart';
import 'session_match.dart';
import 'session_pattern.dart';

/// Keeps track of instances of Module along with the input that is passed to
/// the instance (and whose presence causes the instance to be created in the
/// first place).
class ModuleInstance implements Inspectable {
  final Logger _log = log("ModuleInstance");

  /// The Step in the Recipe this ModuleInstance tracks.
  final Step step;

  /// The match of the scope expression in the recipe step, which gave rise to
  /// this module instance.
  final SessionMatch scopeMatch;

  /// The matches where the input matches of the module are anchored. For module
  /// instances created by the recipe runner, these are the matches of the input
  /// expressions in the recipe step.
  final List<SessionMatch> anchorMatches;

  /// The anchor set that the session matches in the inputs, below, are computed
  /// against.
  final Set<Node> anchorSet = new Set<Node>();

  /// The inputs of the module instances matched from the input expressions in
  /// the manifest. All matches share the anchor set above.
  final List<SessionPattern> inputs = <SessionPattern>[];

  /// The corresponding Manifest matching the Step and the input values, if
  /// applicable.
  final Manifest manifest;

  /// The helper to map labels on edges output by the module back to labels
  /// expected by the recipe.
  final ModuleLabelMapper labelMapper;

  /// Records which edges are output by this module instance. The matches of the
  /// output expressions in the recipe step and manifest are not enough, because
  /// the output expressions equally match the outputs of all module instances.
  /// The bindings record the additional missing information which output match
  /// belongs to which input match.
  final Binding binding;

  /// The Session that this ModuleInstance belongs to.
  final Session session;

  /// An ID that allows us to reference the instance from the command line, and
  /// a global counter to initialize it from.
  static final Incrementer _incrementer = new Incrementer();
  final int id = _incrementer.next;

  /// Runner for this module instance.
  final ModuleRunner _runner;
  bool _running = false;

  final InspectorJSONServer _inspector;

  ModuleInstance(
      final Step _step,
      this.scopeMatch,
      final List<SessionMatch> _anchorMatches,
      final Manifest _manifest,
      this._runner,
      final Session session,
      this._inspector)
      : step = _step,
        session = session,
        anchorMatches = _anchorMatches,
        manifest = _manifest,
        labelMapper = new ModuleLabelMapper(_step, _manifest),
        binding = session.metadata.getBinding(
            _step.verb?.label?.uri?.toString(),
            _toEdges(_anchorMatches).toSet()) {
    _log.info("Creating instance ${manifest?.url}#$id");

    _computeAnchorSet();

    for (final PathExpr expr in []
      ..addAll(manifest.input)
      ..addAll(manifest.compose)) {
      // Path expression matches are updated in updateMatches() after creation.
      inputs.add(new SessionPattern(expr, anchorSet, footprint: true));
    }

    _inspector?.publish(this);
  }

  ModuleRunner get runner => _runner;

  /// Returns true if the manifest supports display composition.
  bool get isDisplayModule => manifest.display.isNotEmpty;

  /// Updates the matches in one ModuleInstance with edges and nodes. Also takes
  /// care to update the anchor set if necessary. Then notifies the module
  /// runner if necessary.
  void updateMatches(final SessionMatchUpdate updateData) {
    _updateAnchorSet();

    for (final SessionPattern moduleInput in inputs) {
      moduleInput.deleteEdges(updateData);
      moduleInput.updateMatches(updateData);
    }

    bool firstRun = false;
    if (!_running && inputs.every((SessionPattern p) => p.isComplete)) {
      _runner?.start(this);
      _running = true;
      firstRun = true;
    }

    if (_running && (firstRun || _graphHasChanged())) {
      _runner.update();
    }
  }

  void _updateAnchorSet() {
    if (!anchorMatches.any((SessionMatch m) => m.hasNewData)) {
      return;
    }

    _computeAnchorSet();

    for (final SessionPattern input in inputs) {
      input.updateAnchorSet(anchorSet);
    }
  }

  void _computeAnchorSet() {
    anchorSet.clear();
    for (final SessionMatch m in anchorMatches) {
      anchorSet.addAll(m.targets);
    }
  }

  /// If the anchor matches have changed, writes the anchor edges as inputs to
  /// the graph. This is done separately from updateMatches() because it changes
  /// the graph, and so invalidates edges that need to be traversed during
  /// updateMatches().
  void updateBinding() {
    if (!anchorMatches.any((SessionMatch m) => m.hasNewData)) {
      return;
    }

    binding.setInput(_toEdges(anchorMatches));
  }

  /// Clears empty matches, except for the first. Resets dirty flags.
  void clearMatches() {
    for (final SessionPattern input in inputs) {
      input.clearMatches();
    }
  }

  /// Returns the session matches associated with the inputs of this module
  /// instance. The input session matches are restricted to the anchor set of
  /// this module instance. However, matches outside the anchor set are not
  /// entirely suppressed, but they yield incomplete matches, which are just
  /// filtered out.
  ///
  /// TODO(mesch): Matches for inputs outside of the scope may flap from one
  /// match to another as we select the first complete match. Not sure yet what
  /// to do about it, if anything.
  List<SessionMatch> get inputMatches => inputs
      .map((SessionPattern input) => input.matches
          .firstWhere((SessionMatch m) => m.isComplete, orElse: () => null))
      .where((SessionMatch m) => m != null)
      .toList();

  /// Obtains all edges in the input matches of the module instance.
  Iterable<Edge> get inputEdges => _toEdges(inputMatches);

  /// Obtains all output edges.
  Iterable<Edge> get outputEdges => binding.loadOutputEdges();

  static Iterable<Edge> _toEdges(final List<SessionMatch> matches) =>
      matches.expand((SessionMatch match) => match.matchedEdges);

  /// Returns a lazy iterable over the edges in this module instance's footprint
  /// graph.
  Iterable<Edge> get footprintEdges {
    return <Iterable<Edge>>[inputEdges, outputEdges]
        .expand((final Iterable<Edge> nested) => nested);
  }

  /// Returns a lazy iterable over the nodes in this module instance's footprint
  /// graph.
  Iterable<Node> get footprintNodes {
    final Iterable<Edge> edges = footprintEdges;

    // The session root goes first, as modules use it for output by default.
    return <Iterable<Node>>[
      <Node>[session.root],
      edges.expand((final Edge e) => <Node>[e.origin, e.target])
    ].expand((final Iterable<Node> nodes) => nodes);
  }

  /// Determines whether the part of the Graph exposed to this Module has
  /// changed since we last checked. Looks for the dirty flag hasNewData on all
  /// its input session matches.
  ///
  /// TODO(mesch): It should possibly also look on the scope match if present,
  /// but that's only because the scope match needs to be forwarded to the
  /// module graph in order to be recorded in the bindings.
  bool _graphHasChanged() =>
      inputMatches.any((SessionMatch match) => match.hasNewData);

  void destroy() {
    _log.info("Destroying instance ${manifest?.url}#$id");
    _inspector?.unpublish(inspectorPath);
    _runner?.stop();
  }

  @override
  String toString() => "$runtimeType:$id:${manifest?.url}";

  @override
  Future<Map<String, dynamic>> inspectorJSON() async {
    return {
      'type': 'moduleInstance',
      'id': id,
      'session': session?.inspectorPath,
      'url': manifest?.url,
      'inputMatches': inputMatches.map((SessionMatch sm) => sm.inspectorJSON()),
      'outputEdges': outputEdges,
    };
  }

  @override // Inspectable
  Future<dynamic> onInspectorPost(dynamic json) async {}

  // TODO: implement inspectorPath
  @override
  String get inspectorPath => '/module-instance/$id';
}
