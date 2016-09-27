// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:parser/expression.dart';

import 'query.dart';

/// Converts from the parse-tree [parser.PatternExpr] format into the modular
/// internal representation [GraphQuery].
GraphQuery patternExprToGraphQuery(final PatternExpr input) {
  final List<String> inEdgeLabels = new List<String>.from(
      input.property.labels.map((Label each) => each.uri.toString()));
  final List<String> valueLabels = new List<String>.from(
      input.property.representations.map((Label each) => each.uri.toString()));
  bool isRequired = true;
  bool isRepeated = false;
  if (input.property.cardinality.isOptional) isRequired = false;
  if (input.property.cardinality.isRepeated) isRepeated = true;

  return new GraphQuery(inEdgeLabels,
      isRequired: isRequired,
      isRepeated: isRepeated,
      valueLabels: valueLabels,
      childConstraints: input.children.map(patternExprToGraphQuery).toList());
}

/// Converts from the parse-tree [parser.PathExpr] format into the modular
/// internal representation [GraphQuery].
GraphQuery pathExprToGraphQuery(final PathExpr input) {
  final PatternExpr patternExpr = input.properties.reversed.fold(
      null,
      (final PatternExpr childExpr, final Property parentProperty) =>
          new PatternExpr(
              parentProperty, childExpr == null ? null : [childExpr]));

  return patternExprToGraphQuery(patternExpr);
}
