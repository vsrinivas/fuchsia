// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:parser/cardinality.dart';
import 'package:parser/expression.dart';
import 'package:parser/manifest.dart';
import 'package:parser/recipe.dart';

/// Finds all manifests that match a step of a recipe.
class ManifestMatcher {
  final List<Manifest> _manifests;

  ManifestMatcher(final List<Manifest> manifests)
      : this._manifests = manifests ?? <Manifest>[];

  List<Manifest> get manifests => new List<Manifest>.unmodifiable(_manifests);

  /// Selects a manifest from the set of known manifests matching a given recipe
  /// step.
  ///
  /// A manifest matches if the verb matches that of the step and the first
  /// segments of all its input and output expressions (collectively called
  /// footprint) match the last segment of some input or output expression
  /// (collectively called scope) in the step. The nodes at which they overlap
  /// are collectively called anchors.
  ///
  /// TODO(thatguy): Querying the index of manifests should happen by talking to
  /// IndexerClient, rather than here.
  Manifest selectManifest(final Step step) {
    final List<PathExpr> stepInputOutput =
        [step.input, step.output].expand(_id).toList();

    final List<PathExpr> stepInputOutputCompose =
        [step.input, step.output, step.compose].expand(_id).toList();

    final List<PathExpr> stepInputOutputComposeDisplay = [
      step.input,
      step.output,
      step.compose,
      step.display
    ].expand(_id).toList();

    for (final Manifest m in _manifests) {
      if (step.verb != m.verb) {
        continue;
      }

      if (step.url != null && step.url != m.url) {
        continue;
      }

      final List<PathExpr> manifestInputOutputComposeDisplay =
          [m.input, m.output, m.compose, m.display].expand(_id).toList();

      // Every input in the manifest must be anchored in the inputs of the step.
      if (!m.input
          .every((final PathExpr expr) => _containsAnchor(step.input, expr))) {
        continue;
      }

      // If the step mentions inputs, they must be somewhere in the manifest
      // too. However, it doesn't have to be in the inputs of the manifest. For
      // example, the recipe may supply the manifest with a node to display
      // something on.
      if (!step.input.every((final PathExpr expr) =>
          _containsAnchorRev(expr, manifestInputOutputComposeDisplay))) {
        continue;
      }

      // Every compose input in the manifest must be anchored in the inputs or
      // outputs of the step, or in the compose inputs of the step if there are
      // any.
      //
      // If the compose expression is not simple, then its last segment is the
      // actual compose input, and the other segments (specifically the root
      // segment) are data, which must connect to either the input or output
      // anchors of the step.
      //
      // If, however, the compose expression is simple, then the single path
      // segment is the compose input itself, which does not need to be
      // mentioned in the step. Hence, a simple compose expression always
      // matches.
      if (!m.compose.every((final PathExpr expr) =>
          expr.isSimple || _containsAnchor(stepInputOutputCompose, expr))) {
        continue;
      }

      // If the step mentions compose inputs, they must appear as compose inputs
      // in the manifest too.
      if (!step.compose.every(
          (final PathExpr expr) => _containsAnchorRev(expr, m.compose))) {
        continue;
      }

      // Every output of the manifest must be anchored in the outputs of the
      // step if it is simple, or in the inputs or outputs of the step if it's a
      // path.
      //
      // NOTE(mesch): Once we support wildcards, presumably we won't need to
      // anchor outputs in input expressions anymore, and the distinction made
      // here may go away.
      if (!m.output.every((final PathExpr expr) => expr.isSimple
          ? _containsAnchor(step.output, expr, ignoreCardinality: true)
          : _containsAnchor(stepInputOutput, expr, ignoreCardinality: true))) {
        continue;
      }

      // If the step mentions outputs, they must be in the manifest too.
      if (!step.output
          .every((final PathExpr expr) => _containsAnchorRev(expr, m.output))) {
        continue;
      }

      // Every display output of the manifest must be anchored anywhere in the
      // step. If the display expression is simple, then it always matches for
      // the same reason as explained for compose expressions.
      if (!m.display.every((final PathExpr expr) =>
          expr.isSimple ||
          _containsAnchor(stepInputOutputComposeDisplay, expr,
              ignoreCardinality: true))) {
        continue;
      }

      // If the step mentions display outputs, they must be as display outputs
      // in the manifest too.
      if (!step.display.every((final PathExpr expr) =>
          _containsAnchorRev(expr, m.display, ignoreCardinality: true))) {
        continue;
      }

      return m;
    }

    return null;
  }

  void addOrUpdateManifest(final Manifest updatedManifest) {
    final Manifest currentManifest = _manifests.firstWhere(
        (final Manifest m) => m.url == updatedManifest.url,
        orElse: () => null);
    _manifests?.remove(currentManifest);
    _manifests?.add(updatedManifest);
  }

  /// Computes whether the given footprint expression is anchored in the given
  /// set of scope expressions, optionally ignoring cardinality of the
  /// connecting path expression segment according to the flag.
  bool _containsAnchor(
      final List<PathExpr> scopeExprs, final PathExpr footprintExpr,
      {final bool ignoreCardinality: false}) {
    final Property footprintAnchor = footprintExpr.properties.first;

    for (final PathExpr scopeExpr in scopeExprs) {
      final Property scopeAnchor = scopeExpr.properties.last;
      if (scopeAnchor.labels.containsAll(footprintAnchor.labels) &&
          (ignoreCardinality ||
              _cardinalityIsCompatible(
                  _sumCardinality(scopeExpr.properties).last,
                  footprintAnchor.cardinality))) {
        return true;
      }
    }

    return false;
  }

  /// Computes whether the given scope expression anchors any one of the given
  /// footprint expressions, optionally ignoring cardinality of the connecting
  /// path expression segment according to the flag.
  bool _containsAnchorRev(
      final PathExpr scopeExpr, final List<PathExpr> footprintExprs,
      {final bool ignoreCardinality: false}) {
    final Property scopeAnchor = scopeExpr.properties.last;

    for (final PathExpr footprintExpr in footprintExprs) {
      final Property footprintAnchor = footprintExpr.properties.first;
      if (scopeAnchor.labels.containsAll(footprintAnchor.labels) &&
          (ignoreCardinality ||
              _cardinalityIsCompatible(
                  _sumCardinality(scopeExpr.properties).last,
                  footprintAnchor.cardinality))) {
        return true;
      }
    }

    return false;
  }

  /// Determines whether the cardinality qualifier in a path expression in
  /// a step is compatible with the cardinality qualifier in a path
  /// expression in a manifest.
  ///
  /// Path segments in inputs of step and manifest with different
  /// cardinality match like this:
  ///
  ///   manifest          singular optional repeated optionalRepeated
  /// step
  ///  singular              v         v        v          v
  ///  optional              x         v        x          v
  ///  repeated              x         x        v          v
  ///  optionalRepeated      x         x        x          v
  ///
  /// v - match
  /// x - no match
  static bool _cardinalityIsCompatible(
      final Cardinality step, final Cardinality manifest) {
    if (step.isOptional && !manifest.isOptional) {
      return false;
    }

    if (step.isRepeated && !manifest.isRepeated) {
      return false;
    }

    return true;
  }

  static List<Cardinality> _sumCardinality(final List<Property> path) {
    final List<Cardinality> ret = <Cardinality>[];
    Cardinality sum = Cardinality.singular;
    for (final Property prop in path) {
      sum = sum.sum(prop.cardinality);
      ret.add(sum);
    }
    return ret;
  }

  static List<PathExpr> _id(final List<PathExpr> i) => i;
}
