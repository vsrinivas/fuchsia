// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:common/incrementer.dart';
import 'package:modular_core/graph/graph.dart' show Edge, Node;
import 'package:parser/expression.dart' show PathExpr;

/// An output of a ModuleInstance. Used by the simulation runner to keep track
/// of whether it was emitted.
class SimulationModuleOutput {
  // The output expression.
  final PathExpr expr;

  // The values this output has emitted, expressed as SimulationOutputValue
  // instances.
  final List<SimulationOutputValue> emittedValues = <SimulationOutputValue>[];

  // An ID that allows us to reference the instance from the command line, and a
  // global counter to initialize it from.
  static final Incrementer _incrementer = new Incrementer();
  final int id = _incrementer.next;

  SimulationModuleOutput(this.expr);

  /// Returns a [SimulationOutputValue] of the given scope. If such an
  /// [SimulationOutputValue] already exists, it returns it; otherwise it is
  /// created.
  SimulationOutputValue getOpenOutput(final Node scope) {
    for (final SimulationOutputValue value in emittedValues) {
      if (value.scope == scope) {
        return value;
      }
    }

    final SimulationOutputValue newValue =
        new SimulationOutputValue(expr, scope);
    emittedValues.add(newValue);

    return newValue;
  }

  @override
  String toString() => "$runtimeType:$expr/$emittedValues";
}

/// A value emitted by a SimulationModuleOutput.
class SimulationOutputValue {
  final PathExpr expr;
  final Node scope;

  /// Holds the actual output edges. Each element in outer dimension corresponds
  /// to a component in expr.properties; each element in the inner dimension
  /// corresponds to a repeated edge for the same component.
  final List<Set<Edge>> targets = <Set<Edge>>[];

  SimulationOutputValue(this.expr, this.scope) {
    targets.length = expr.properties.length;
    for (int i = 0; i < targets.length; ++i) {
      targets[i] = new Set<Edge>();
    }
  }

  /// Adds the edge at the given index of the path expression.
  void addTarget(int index, final Edge edge) {
    targets[index].add(edge);
  }

  /// Returns the edge at the given index of the path expression linking to the
  /// provided node, null if it doesn't exist.
  Edge getTarget(final int index, final Node node) {
    for (final Edge edge in targets[index]) {
      if (edge.target == node) {
        return edge;
      }
    }
    return null;
  }

  @override
  String toString() => "$runtimeType:$targets";
}
