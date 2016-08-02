// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:collection/collection.dart';
import 'package:modular_core/graph/graph.dart';
import 'package:parser/expression.dart';
import 'package:parser/recipe.dart';
import 'package:parser/manifest.dart';

/// The result of a match between an edge and a list of output path expressions.
class _PathMatch {
  /// Index of the output path expression matched.
  final int outputIndex;

  /// Index of the matched component within the path expression
  final int matchIndex;

  _PathMatch(this.outputIndex, this.matchIndex);

  @override
  String toString() => '$runtimeType: $outputIndex : $matchIndex';
}

/// Maps from edge labels as defined in a Module's manifest (and output by a
/// Module) to those defined in a recipe Step that matched the Module.
///
/// The labels in the recipe step may be a superset of the labels in the
/// manifest, and when a module outputs an edge with only the label(s) declared
/// in its manifest, the edge must be added to the session graph with all the
/// labels declared in the recipe step.
class ModuleLabelMapper {
  final Manifest _manifest;
  final Step _step;
  ModuleLabelMapper(this._step, this._manifest);

  /// Finds a component of an output path expression in the Manifest of the
  /// module, and returns the labels specified in the corresponding path
  /// expression component as defined in the recipe step that matched this
  /// module, if they overlap. Manifest expressions and step expressions overlap
  /// only at the anchors, i.e. the first segments of the manifest expressions.
  Iterable<String> getRecipeLabels(
      final Node source, final Set<String> moduleLabels) {
    final _PathMatch match = _matchEdge(source, moduleLabels);

    if (match != null) {
      if (match.matchIndex > 0) {
        return moduleLabels;
      } else {
        return _stepLabels(moduleLabels);
      }
    }

    return null;
  }

  Set<String> getValueLabels(final Node source) {
    final Set<Label> allowedRepresentations = new Set<Label>();

    for (final Edge inEdge in source.inEdges) {
      final _PathMatch match = _matchEdge(inEdge.origin, inEdge.labels.toSet());
      if (match == null) {
        continue;
      }

      allowedRepresentations.addAll(_manifest.output[match.outputIndex]
          .properties[match.matchIndex].representations);
    }

    return allowedRepresentations
        .map((final Label label) => label.uri.toString())
        .toSet();
  }

  Set<String> _stepLabels(final Set<String> moduleLabels) {
    for (final PathExpr expr in _step.output) {
      final Set<String> stepLabels = expr.properties.last.labels
          .map((final Label label) => label.uri.toString())
          .toSet();
      if (stepLabels.containsAll(moduleLabels)) {
        return stepLabels;
      }
    }
    return moduleLabels;
  }

  _PathMatch _matchEdge(final Node source, final Set<String> moduleLabels) {
    for (int i = 0; i < _manifest.output.length; ++i) {
      final PathExpr expr = _manifest.output[i];
      final int matchIndex = _findMatchInPathExpr(expr, moduleLabels);

      if (matchIndex != -1 &&
          _doesPreviousMatch(source, expr, matchIndex - 1)) {
        return new _PathMatch(i, matchIndex);
      }
    }

    return null;
  }

  bool _doesPreviousMatch(
      final Node node, final PathExpr path, final int index) {
    if (index < 0) {
      return true;
    }

    final Set<String> labelSet = path.properties[index].labels
        .map((final Label label) => label.uri.toString())
        .toSet();

    return node.inEdges
        .where((final Edge edge) => edge.labels.toSet().containsAll(labelSet))
        .any((final Edge edge) =>
            _doesPreviousMatch(edge.origin, path, index - 1));
  }

  int _findMatchInPathExpr(
      final PathExpr path, final Set<String> moduleLabels) {
    for (int i = path.properties.length - 1; i >= 0; --i) {
      final Set<String> labelSet = path.properties[i].labels
          .map((final Label label) => label.uri.toString())
          .toSet();

      if (const SetEquality<String>().equals(labelSet, moduleLabels)) {
        return i;
      }
    }

    return -1;
  }
}
