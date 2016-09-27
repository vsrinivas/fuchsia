// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:typed_data';

import 'package:collection/collection.dart';
import 'package:parser/expression.dart' as parser;
import 'package:parser/expression_parser.dart' as parser;
import 'package:parser/parse_error.dart' as parser;

import '../../util/hash.dart';
import '../graph.dart';
import 'convert.dart';
import 'query_match.dart';

export 'query_match.dart';

/// A [GraphQuery] describes a query over the structure of a graph, allowing one
/// to specify constraints on nodes in the graph, as well as constraints
/// on 'children' (the targets of out-edges) from those nodes.
///
/// The dartdoc below describes how fields would affect the matching process
/// if this [GraphQuery] were to be evaluated against a specific node in a
/// graph.
///
/// See test/graph/query/query_matcher_test.dart for examples of
/// building [GraphQuery]s, and the effect they have when matched against a
/// [Graph].
class GraphQuery {
  /// [isRequired] only has meaning when this [GraphQuery] is the 'child' of
  /// a another (ie, it is included in [childConstraints] on another instance).
  ///
  /// If a [isRequired] is false, it will not fail a parent's match.
  final bool isRequired;

  /// If [isRepeated] is set to true, the matching process will collapse
  /// multiple matching sibling edges into one single match. If [isRepeated] is
  /// false, the same process will produce N matches.
  final bool isRepeated;

  /// If [inEdgeLabels] is empty, this is considered a wildcard match (ie, it
  /// will successfully match against any node). Otherwise, all elements
  /// in [inEdgeLabels] must be found on the labels of at least one incoming
  /// edge to said node to match this [GraphQuery].
  ///
  /// Note that this is an AND condition, and there is no way to express that
  /// a matching edge must contain *any* of the labels in [inEdgeLabels], or to
  /// express that the labels must occur in the union of the labels of all
  /// incoming edges.
  final Set<String> inEdgeLabels;

  /// If [valueLabels] is set, requires that a node being evaluated against
  /// this [GraphQuery] have at least one key in [valueLabels] set as a
  /// value on the node.
  ///
  /// Note that this is an OR condition, and there is no way to express that
  /// a matching node contain *all* the values in [valueLabels].
  final Set<String> valueLabels;

  /// For a node to match this [GraphQuery], every [GraphQuery] in
  /// [childConstraints] must also match against the target of some outgoing
  /// edge on said node, with the exception that if a [childConstraint] is not
  /// required, it will not fail the match but rather mark it as 'incomplete'.
  final Iterable<GraphQuery> childConstraints;

  GraphQuery(Iterable<String> _inEdgeLabels,
      {Iterable<GraphQuery> childConstraints,
      this.isRepeated: false,
      this.isRequired: true,
      Iterable<String> valueLabels: const <String>[]})
      : inEdgeLabels = _inEdgeLabels.toSet(),
        childConstraints = (childConstraints ?? const <GraphQuery>[]).toList(),
        this.valueLabels = valueLabels.toSet();

  factory GraphQuery.fromPatternExpr(final parser.PatternExpr expr) {
    return patternExprToGraphQuery(expr);
  }

  factory GraphQuery.fromString(final String text) {
    final parser.Scanner scanner = new parser.Scanner(
        new parser.ParserState(), new parser.SourceLocation.inline(), text);
    final parser.PatternExpr expr = parser.parsePattern(scanner);
    scanner.checkDone();
    return patternExprToGraphQuery(expr);
  }

  factory GraphQuery.fromJSON(final Object json) {
    // Allow recursive application to 'childConstraints'.
    final dynamic decoded = json is String ? JSON.decode(json) : json;
    assert(decoded['type'] == 'GraphQuery' && decoded['version'] == 1);
    final Map<String, dynamic> data = decoded['data'];
    return new GraphQuery(data['inEdgeLabels'],
        childConstraints: data['childConstraints']
            .map((Object json) => new GraphQuery.fromJSON(json)),
        isRepeated: data['isRepeated'],
        isRequired: data['isRequired'],
        valueLabels: data['valueLabels']);
  }

  factory GraphQuery.fromUint8List(Uint8List list) =>
      new GraphQuery.fromJSON(UTF8.decode(list));

  /// See doc for [inEdgeLabels].
  bool get isWildcard => inEdgeLabels.isEmpty;

  /// Convenient way to get matches; delegates to [GraphQueryMatcher].
  /// TODO(thatguy): Update callers to use [Graph.query()] and remove this.
  List<GraphQueryMatch> match(Graph graph, {bool keepRootEdge: false}) =>
      new GraphQueryMatcher(graph).match(this, keepRootEdge: keepRootEdge);

  /// Returns false if this GraphQuery is invalid given the assumptions made
  /// by the GraphQuery matching algorithm.
  ///
  /// This method is to be called only on a root path expression, never on a
  /// [GraphQuery] that appears in the childConstraints of another
  /// [GraphQuery].
  bool validate() {
    // TODO(thatguy): Include more diagnostic information with a failed
    // validation (ie, the GraphQuery text, the failed component, etc).

    // The root of a [GraphQuery] is always required.
    if (!isRequired) return false;
    // Value constraints are not allowed on wildcard nodes.
    if (isWildcard && valueLabels.isNotEmpty) return false;
    return childConstraints
        .every((GraphQuery each) => each._validateChildRecursive());
  }

  String toJSON() {
    final List<dynamic> children = childConstraints
        .map((GraphQuery child) => JSON.decode(child.toJSON()))
        .toList();

    return JSON.encode({
      'type': 'GraphQuery',
      'version': 1,
      'data': {
        'isRequired': isRequired,
        'isRepeated': isRepeated,
        'inEdgeLabels': inEdgeLabels.toList(),
        'valueLabels': valueLabels.toList(),
        'childConstraints': children
      }
    });
  }

  Uint8List toUint8List() => new Uint8List.fromList(UTF8.encode(toJSON()));

  bool _validateChildRecursive() {
    // We are looking at a non-root GraphQuery now.

    // Wildcards are only allowed at the root.
    if (isWildcard) return false;
    return childConstraints
        .every((GraphQuery each) => each._validateChildRecursive());
  }

  @override
  bool operator ==(other) =>
      other is GraphQuery &&
      isRequired == other.isRequired &&
      isRepeated == other.isRepeated &&
      const SetEquality<String>().equals(inEdgeLabels, other.inEdgeLabels) &&
      const SetEquality<String>().equals(valueLabels, other.valueLabels) &&
      const ListEquality<GraphQuery>()
          .equals(childConstraints, other.childConstraints);

  @override
  int get hashCode => hashObjects(
      [isRequired, isRepeated, inEdgeLabels, valueLabels, childConstraints]);

  @override
  String toString() {
    final String labels = isWildcard ? "_" : inEdgeLabels.join(' ');
    final String wrappedLabels = inEdgeLabels.length > 1 ? '($labels)' : labels;
    String cardinality = '';
    if (isRequired && isRepeated) cardinality = '+';
    if (!isRequired && isRepeated) cardinality = '*';
    if (!isRequired && !isRepeated) cardinality = '?';
    final String values =
        valueLabels.isEmpty ? '' : ' <' + valueLabels.join(', ') + '>';
    final String children = childConstraints.isEmpty
        ? ''
        : childConstraints.map((GraphQuery each) => each.toString()).join(', ');
    final String wrappedChildren = childConstraints.length == 1
        ? ' -> $children'
        : (childConstraints.length > 1 ? ' {$children}' : children);
    return '$wrappedLabels$values$cardinality$wrappedChildren';
  }
}
