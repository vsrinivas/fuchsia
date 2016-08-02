// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';
import 'package:handler/graph/session_graph.dart';
import 'package:handler/module_instance.dart';
import 'package:handler/module_runner.dart';
import 'package:modular_core/graph/graph.dart';
import 'package:parser/expression.dart' show Label, PathExpr, Property;
import 'package:parser/manifest.dart' show Manifest;

import 'simulation_module_output.dart';

class SimulationModuleRunner implements ModuleRunner {
  final SimulationModuleRunnerFactory _factory;
  final Map<int, bool> _didOutput = new Map<int, bool>();

  static final List<Label> _suggestinatorLabels = <Label>[
    new Label.fromUri(Uri.parse(
        'https://github.com/domokit/modular/wiki/semantic#speech-input')),
    new Label.fromUri(
        Uri.parse('https://github.com/domokit/modular/wiki/semantic#story')),
    new Label.fromUri(Uri
        .parse('https://github.com/domokit/modular/wiki/semantic#session-id'))
  ];

  // The module instance we simulate running a module app for.
  ModuleInstance instance;

  // The output instances emitted from the module instance.
  final List<SimulationModuleOutput> outputs = <SimulationModuleOutput>[];

  SimulationModuleRunner(this._factory);

  @override
  void start(final ModuleInstance instance) {
    this.instance = instance;

    final List<PathExpr> outputExprs = []
      ..addAll(instance.step.output)
      ..addAll(instance.step.display);

    for (int i = 0; i < outputExprs.length; ++i) {
      if (outputExprs[i].properties.any((final Property property) =>
          _suggestinatorLabels
              .any((final Label label) => property.labels.contains(label)))) {
        continue;
      }

      outputs.add(new SimulationModuleOutput(outputExprs[i]));
    }

    _factory.onStart(this);
  }

  @override
  void update() {}

  @override
  void stop() {}

  /// Collects all representation types that a manifest declares for a given
  /// Label. Used to create dummy representation values on dummy outputs.
  List<Label> _selectRepresentation(
      final Manifest manifest, final Property property) {
    final List<Label> ret = <Label>[];
    for (final Label l in property.labels) {
      for (final PathExpr output in manifest.output) {
        for (final Property property in output.properties) {
          if (property.labels.contains(l)) {
            ret.addAll(property.representations);
          }
        }
      }
    }
    return ret;
  }

  /// Inserts a dummy output value into the graph, and adjusts the session
  /// accordingly.
  bool _triggerOutput(final SimulationModuleOutput output) {
    if (_didOutput.containsKey(output.id)) {
      return false;
    }
    _didOutput[output.id] = true;

    Node current;
    int start = 0;
    if (instance.scopeMatch != null) {
      // If the step has a scope, outputs are emitted as properties of the
      // scope entity. If there is a scope, it must be matched, otherwise
      // the instance would not exist, and it is of singular cardinality.
      current = instance.scopeMatch.targets.single;

      // If the output is specified as a path, and the scope path expression is
      // a prefix of the output path expression, then the prefix is not
      // duplicated.
      if (instance.step.scope.isPrefixOf(output.expr)) {
        start = instance.step.scope.properties.length;
      }
    } else {
      // The scope is the session.
      current = instance.session.graph.root;
    }

    final SimulationOutputValue outputValue = output.getOpenOutput(current);
    instance.session.graph.mutate((final GraphMutator mutator) {
      for (int i = start; i < output.expr.properties.length; i++) {
        final Property property = output.expr.properties[i];
        final Iterable<String> labels =
            property.labels.map((final Label l) => l.uri.toString());

        final Iterable<Edge> edge = current.outEdgesWithLabels(labels);
        if (edge.isNotEmpty) {
          current = edge.first.target;
          outputValue.addTarget(i, edge.first);
        } else {
          final Node newNode = mutator.addNode();
          final Edge newEdge = mutator.addEdge(current.id, labels, newNode.id);
          current = newNode;
          outputValue.addTarget(i, newEdge);
        }

        if (instance.manifest != null) {
          for (final Label r
              in _selectRepresentation(instance.manifest, property)) {
            mutator.setValue(
                current.id, r.toString(), new Uint8List.fromList([0]));
          }
        }
      }
    });

    return true;
  }

  /// Inserts a dummy output value into the graph, and adjusts the session
  /// accordingly.
  bool triggerOutput(final int id) {
    if (_didOutput.containsKey(id)) {
      return false;
    }

    for (final SimulationModuleOutput output in outputs) {
      if (output.id == id) {
        return _triggerOutput(output);
      }
    }
    _didOutput[id] = false;
    return false;
  }

  /// Inserts a dummy output value for each possible output into the graph, and
  /// adjusts the session accordingly.
  bool triggerOutputInstance() {
    bool ret = false;
    for (final SimulationModuleOutput output in outputs) {
      if (_triggerOutput(output)) {
        ret = true;
      }
    }
    return ret;
  }
}

/// A ModuleRunnerFactory that allows to simulate module outputing values.
class SimulationModuleRunnerFactory {
  final Map<int, SimulationModuleRunner> _runners =
      <int, SimulationModuleRunner>{};

  ModuleRunner call() {
    return new SimulationModuleRunner(this);
  }

  void onStart(SimulationModuleRunner runner) {
    _runners[runner.instance.id] = runner;
  }

  /// Adds dummy output from the output instance given by its ID. Used by the
  /// runner.
  void triggerOutput(final int id) {
    for (final SimulationModuleRunner runner in _runners.values.toList()) {
      runner.triggerOutput(id);
    }
  }

  /// Adds dummy output from all output instances of the module instance given
  /// by its ID. Used by the runner.
  void triggerOutputInstance(final int id) {
    _runners[id].triggerOutputInstance();
  }

  /// Adds dummy output from all output instances from which not output was
  /// added yet, creating new module instances, recursively until there are no
  /// fresh output instances anymore. Used by the runner.
  void triggerOutputAll() {
    bool runAgain = true;
    while (runAgain) {
      runAgain = false;
      for (final SimulationModuleRunner runner in _runners.values.toList()) {
        if (runner.triggerOutputInstance()) {
          runAgain = true;
        }
      }
    }
  }
}
