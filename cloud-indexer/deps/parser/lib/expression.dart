// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This file defines the data structures that comprise the abstract syntax tree
/// of expressions parsed by the expression parser.

import 'package:collection/collection.dart';
import 'package:yaml/yaml.dart';
import 'package:modular_core/entity/schema.dart' as entity;
import 'cardinality.dart';
import 'parse_error.dart';

class Label {
  /// Syntactic entities (verbs, properties, representation types, display
  /// types) in modular are identified by URIs. Such IDs are also called
  /// semantic labels, because they represent meaning across recipes and
  /// manifests.
  final Uri uri;

  /// In source text semantic labels are referred to by locally defined,
  /// "shorthand", names for conciseness. The URI is globally unambiguous, and
  /// the shorthand is unambiguous within the source file.
  final String shorthand;

  Label(this.uri, this.shorthand);

  factory Label.copy(final Label o) => new Label(o.uri, o.shorthand);

  Label.fromUri(final Uri uri)
      : uri = uri,
        shorthand = uri.toString();
  Label.fromUriString(final String uriString)
      : uri = Uri.parse(uriString),
        shorthand = uriString;

  factory Label.fromJson(Map<String, dynamic> values) {
    assert(values != null);
    return new Label(Uri.parse(values['uri']), values['shorthand']);
  }

  dynamic toJson() => {'uri': uri.toString(), 'shorthand': shorthand};

  @override
  bool operator ==(o) => o is Label && uri == o.uri;

  @override
  int get hashCode => uri.hashCode;

  @override
  String toString() => shorthand ?? uri.toString();
}

/// Identifies one or more semantic nodes by labels on its incoming edges and
/// specifies the allowed representation types of these nodes.
///
/// If labels is empty, it represents a wildcard _.
class Property {
  /// The set of semantic labels on an edge matched.
  final Set<Label> labels;

  /// How many edges are matched by a single match of this expression.
  ///
  /// TODO(mesch): This is not final because it's adjusted in a post traversal
  /// for output expressions. Soon the distinction of output and input
  /// expressions is dropped in favor of footprint expressions, in which
  /// cardinality is allowed everywhere.
  /* final */ Cardinality cardinality;

  /// The allowed representation types on the nodes at the target of the matched
  /// edges.
  final Set<Label> representations;

  Property(final Iterable<Label> labels,
      [final Cardinality cardinality_, final Iterable<Label> representations])
      : labels = new Set<Label>.from(labels ?? []),
        cardinality = cardinality_ ?? Cardinality.singular,
        representations = new Set<Label>.from(representations ?? []);

  factory Property.copy(final Property o) => new Property(
      o.labels.map((Label l) => new Label.copy(l)),
      o.cardinality,
      o.representations.map((Label l) => new Label.copy(l)));

  factory Property.fromJson(Map<String, dynamic> values) => new Property(
      values['labels'].map((i) => new Label.fromJson(i)),
      new Cardinality.fromJson(values['cardinality']),
      new Set<Label>.from(
          values['representations'].map((i) => new Label.fromJson(i))));

  dynamic toJson() => {
        'labels': labels.toList(),
        'cardinality': cardinality,
        'representations': representations.toList()
      };

  bool equalsIgnoringCardinality(o) =>
      o is Property &&
      const SetEquality().equals(labels, o.labels) &&
      const SetEquality().equals(representations, o.representations);

  @override
  bool operator ==(o) =>
      equalsIgnoringCardinality(o) && cardinality == o.cardinality;

  @override
  int get hashCode =>
      cardinality.hashCode ^
      const SetEquality().hash(labels) ^
      const SetEquality().hash(representations);

  @override
  String toString() =>
      (labels.length > 1
          ? '(${labels.join(" ")})'
          : labels.length == 1 ? labels.join('') : '_') +
      ('$cardinality') +
      (representations.length > 0 ? ' <${representations.join(",")}>' : '');
}

class Verb {
  final Label label;

  const Verb(this.label);

  factory Verb.fromJson(Map<String, dynamic> values) {
    return new Verb(new Label.fromJson(values['label']));
  }

  dynamic toJson() => {'label': label};

  @override
  String toString() => label.toString();

  @override
  bool operator ==(o) => o is Verb && label == o.label;

  @override
  int get hashCode => label.hashCode;
}

/// Represents a complete recursive modular semantic label expression. Example:
///
///   A <E> -> B <F> { C <G>, D <H> }
///
/// Each step in the path is represented as a Property. Subsequent steps are
/// represented recursively as a set of semantic label expressions. A ->
/// expression is represented by a single child; a { ... } expression is
/// represented as multiple children.
///
/// The property in this instance is A <E>, the property in the single child of
/// it is B <F>, which in turn has two children with properties C <G> and D <H>,
/// respectively.
class PatternExpr {
  final Property property;
  final Set<PatternExpr> children;

  PatternExpr(this.property, [final Iterable<PatternExpr> children])
      : children = new Set<PatternExpr>.from(children ?? []);

  factory PatternExpr.copy(final PatternExpr o) => new PatternExpr(
      new Property.copy(o.property),
      o.children.map((PatternExpr e) => new PatternExpr.copy(e)));

  /// Multiplies out all children properties into linear path expressions, i.e.
  /// transforms a single property expression with multiple children into
  /// multiple path expressions with single children, recursively.
  List<PathExpr> flatten() {
    final List<PathExpr> result = <PathExpr>[];
    _flatten(<Property>[], result);
    return result;
  }

  void _flatten(final List<Property> base, final List<PathExpr> result) {
    final newBase = <Property>[]
      ..addAll(base)
      ..add(property);
    if (children.isEmpty) {
      result.add(new PathExpr(newBase));
    } else {
      for (final PatternExpr child in children) {
        child._flatten(newBase, result);
      }
    }
  }

  @override
  String toString() =>
      property.toString() +
      (children.length == 0
          ? ''
          : (children.length == 1
              ? (' -> ' + children.single.toString())
              : (' { ' + children.join(', ') + ' }')));

  @override
  bool operator ==(o) =>
      o is PatternExpr &&
      property == o.property &&
      const SetEquality().equals(children, o.children);

  @override
  int get hashCode => property.hashCode ^ const SetEquality().hash(children);
}

typedef bool PropertyEquality(Property p1, Property p2);

/// The result of flattening a PatternExpr is a PathExpr. It represents a
/// single linear path pattern in a Graph.
class PathExpr {
  final List<Property> properties;

  /// Creates one from a sequence of properties.
  PathExpr(final Iterable<Property> properties)
      : properties = new List<Property>.unmodifiable(properties);

  /// Creates one from a single property. In this case, isSimple is true.
  PathExpr.single(final Property property)
      : properties = new List<Property>.unmodifiable([property]);

  factory PathExpr.fromJson(List<dynamic> values) {
    return new PathExpr(values.map((i) => new Property.fromJson(i)));
  }

  dynamic toJson() => properties;

  /// The number of edge components that need to be matched by this expression.
  int get length => properties.length;

  /// Whether there is only one component in the Path. Single component
  /// expressions are much simpler to handle than general expressions, so
  /// implementations treat them specially.
  bool get isSimple => properties.length == 1;
  bool get isNotSimple => !isSimple;

  /// Returns true if this path expression represents an optional field.
  bool get isOptional =>
      properties.isEmpty || properties.first.cardinality.isOptional;

  /// Determines whether this path expression is a prefix of another one, using
  /// the equality comparison functor for Property instances provided.
  bool isPrefixOf(final PathExpr o, {final PropertyEquality equality: _eq}) {
    if (length > o.length) {
      return false;
    }

    for (int i = 0; i < length; ++i) {
      if (!equality(properties[i], o.properties[i])) {
        return false;
      }
    }

    return true;
  }

  /// Determines whether this path expression is a suffix of another one, using
  /// the equality comparison functor for Property instances provided.
  bool isSuffixOf(final PathExpr o, {final PropertyEquality equality: _eq}) {
    if (length > o.length) {
      return false;
    }

    for (int i = length - 1; i >= 0; --i) {
      if (!equality(properties[i], o.properties[i])) {
        return false;
      }
    }

    return true;
  }

  // Determines whether this path expression is contained in another one, using
  // the equality comparison functor for Property instances provided.
  bool isContainedIn(final PathExpr o, {final PropertyEquality equality: _eq}) {
    if (length > o.length) {
      return false;
    }

    for (int i = 0; i < o.length - length + 1; ++i) {
      if (_propertyListEquality(
          properties, o.properties.sublist(i, i + length), equality)) {
        return true;
      }
    }
    return false;
  }

  // Determines if two property lists are equal, using the equality comparison
  // functor for Property instances provided.
  static bool _propertyListEquality(final List<Property> a,
      final List<Property> b, final PropertyEquality equality) {
    if (a.length != b.length) {
      return false;
    }
    for (int i = 0; i < a.length; ++i) {
      if (!equality(a[i], b[i])) {
        return false;
      }
    }
    return true;
  }

  /// Returns true if any of the components of this path expression is equal to
  /// the given [label].
  bool containsLabel(final Label label) {
    return properties.any((final Property p) => p.labels.contains(label));
  }

  /// Returns true if any of the components of this path expression is equal to
  /// the given [label], where [label] is a string that contains a URI
  /// representing a label.
  bool containsLabelAsString(final String label) {
    return containsLabel(new Label.fromUri(Uri.parse(label)));
  }

  /// A default equality comparison functor to be used in the predicates above.
  /// It respects cardinality but ignores representation labels.
  static bool _eq(final Property p1, final Property p2) =>
      p1.cardinality == p2.cardinality &&
      const SetEquality().equals(p1.labels, p2.labels);

  @override
  String toString() => properties.join(' -> ');

  @override
  bool operator ==(o) =>
      o is PathExpr && const ListEquality().equals(properties, o.properties);

  @override
  int get hashCode => const ListEquality().hash(properties);
}

/// Maintains state established during parsing that is needed to parse more, but
/// is not made part of the resulting parse tree. An instance of this class is
/// passed to essentially every parser function. Alternatively, all parser
/// functions could become methods of this class, but then the parser functions
/// could no longer be in multiple files, so this solution is equivalent
/// semantically but more flexible organizationally.
class ParserState {
  /// Maps shorthand names to global IDs expressed as URIs. The Label
  /// value of this map contains both the shorthand and the URI.
  final Map<String, Label> shorthand = <String, Label>{};

  /// The source location where a shorthand is defined.
  final Map<String, SourceLocation> shorthandLocation =
      <String, SourceLocation>{};

  /// Maps Schema types to Schema objects as defined in a single manifest.
  final Map<Label, entity.Schema> schemas = <Label, entity.Schema>{};

  /// Maps from a Schema's type to where it was defined.
  final Map<Label, SourceLocation> schemaLocation = <Label, SourceLocation>{};

  /// Set of imported files. Used to prevent double import.
  final Set<String> imported = new Set<String>();

  void addShorthand(
      final String name, final Uri uri, final SourceLocation location) {
    shorthand[name] = new Label(uri, name);
    shorthandLocation[name] = location;
  }

  Uses get uses => new Uses._fromMap(shorthand);

  /// Returns a new [SchemaRegistry] for the [Schema]s stored in [schemas].
  entity.SchemaRegistry get schemaRegistry {
    entity.SchemaRegistry registry = new entity.SchemaRegistry();
    schemas.values.forEach(registry.add);
    return registry;
  }
}

/// Keeps the map from shorthands to Labels based on absolute URLs established
/// in the use: section of a recipe or manifest.
class Uses {
  /// Maps shorthand names to global IDs expressed as URIs. The Label
  /// value of this map contains both the shorthand and the URI.
  final Map<String, Label> shorthand = <String, Label>{};

  Uses();

  Uses._fromMap(final Map<String, Label> values) {
    assert(values != null);
    shorthand.addAll(values);
  }

  Uses.fromJson(final Map<String, dynamic> values) {
    assert(values != null);
    values.forEach((final String k, final dynamic v) {
      shorthand[k] = new Label.fromJson(v);
    });
  }

  dynamic toJson() => shorthand;

  @override
  bool operator ==(o) =>
      o is Uses && const MapEquality().equals(shorthand, o.shorthand);

  @override
  int get hashCode => const MapEquality().hash(shorthand);
}

/// Helper function to extend a list of pattern expressions by properties
/// defined in [Schema]s. For each [PatternExpr] in [exprs], if a label at
/// any level is a known [Schema] type, expands the expression with properties
/// that match [Entity]s of that type in the graph.
///
/// [Schema]s that reference each other in a cycle will result in a
/// [ParseError].
void applySchemas(final ParserState parserState, final YamlNode location,
    final Iterable<PatternExpr> exprs) {
  if (exprs == null) {
    return;
  }

  for (final PatternExpr expr in exprs) {
    for (final Label label in expr.property.labels) {
      _schemaToPatternExpr(
          expr, parserState.schemas[label], parserState, location);
    }

    applySchemas(parserState, location, expr.children);
  }
}

/// When a Schema is to be applied to an existing pattern, this
/// function recursively pulls the Schema into the expr.
void _schemaToPatternExpr(final PatternExpr expr, final entity.Schema schema,
    final ParserState parserState, final YamlNode location,
    [final List<String> schemasAlreadyApplied = const []]) {
  if (schema == null) {
    return;
  }

  if (schemasAlreadyApplied.contains(schema.type)) {
    final String schemaStack = schemasAlreadyApplied
        .map((String label) => '${parserState.schemaLocation[label]}'
            ' ${parserState.schemas[label]}')
        .join('\n');
    throw new ParseError.atNode(
        location,
        'Recursive schemas are not supported.'
        ' Applying schema to "$expr" yields'
        ' a recursive schema:\n$schemaStack');
  }

  // Take all Schema properties and add them as children to [expr]. If we find
  // another Schema type, recursively add that property as well.
  schema.properties.forEach((final entity.Property property) {
    final String propertyLabel = schema.propertyLongName(property.name);
    // All properties are optional.
    final Cardinality cardinality = property.isRepeated
        ? Cardinality.optionalRepeated
        : Cardinality.optional;
    final PatternExpr newExpr = new PatternExpr(
        new Property([new Label.fromUriString(propertyLabel)], cardinality));

    if (!property.isBuiltin) {
      // For non-builtin types (other Schema types), we recursively apply
      // the transformation we're doing here.
      _schemaToPatternExpr(
          newExpr,
          parserState.schemaRegistry.get(property.type),
          parserState,
          location,
          new List<String>.from(schemasAlreadyApplied)..add(schema.type));
    }
    expr.children.add(newExpr);
  });
}
