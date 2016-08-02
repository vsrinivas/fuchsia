// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:developer';

import 'package:collection/collection.dart';

import 'cardinality.dart';
import 'expression.dart';
import 'parser.dart' show parseRecipe;
import 'manifest.dart' show Manifest;

class Recipe {
  final String title;
  final Uses use;
  final Verb verb;
  final List<PathExpr> input;
  final List<PathExpr> output;
  final List<Step> steps;
  final Uri test;

  Recipe(this.steps,
      {this.title, this.use, this.verb, this.input, this.output, this.test});

  // Parses a YAML [String] representing a recipe into a [Recipe].
  // Throws a [ParserError] if an error occurs.
  factory Recipe.parseYamlString(String yaml) {
    return parseRecipe(yaml);
  }

  factory Recipe.fromJsonString(String jsonString) {
    // TODO(thatguy): This process lacks adequate validation and error handling
    // when a malformed [jsonString] is given.
    return Timeline.timeSync("Recipe.fromJsonString",
        () => new Recipe.fromJson(JSON.decode(jsonString)));
  }

  factory Recipe.fromJson(Map<String, dynamic> values) {
    return Timeline.timeSync("Recipe.fromJson", () {
      return new Recipe(
          values['steps']?.map((i) => new Step.fromJson(i))?.toList(),
          title: values['title'],
          use: new Uses.fromJson(values['use'] ?? {}),
          verb:
              values['verb'] != null ? new Verb.fromJson(values['verb']) : null,
          input:
              values['input']?.map((i) => new PathExpr.fromJson(i))?.toList(),
          output:
              values['output']?.map((i) => new PathExpr.fromJson(i))?.toList());
    });
  }

  String toJsonString() {
    return Timeline.timeSync("$runtimeType toJson", () => JSON.encode(this));
  }

  dynamic toJson() {
    return Timeline.timeSync(
        "$runtimeType toJson",
        () => {
              'title': title,
              'use': use,
              'verb': verb,
              'input': input,
              'output': output,
              'steps': steps
            });
  }

  @override
  String toString() => title ?? verb?.toString() ?? "<untitled>";

  @override
  bool operator ==(rhs) {
    return rhs is Recipe &&
        title == rhs.title &&
        use == rhs.use &&
        verb == rhs.verb &&
        const ListEquality().equals(input, rhs.input) &&
        const ListEquality().equals(output, rhs.output) &&
        const ListEquality().equals(steps, rhs.steps) &&
        test == rhs.test;
  }

  @override
  int get hashCode {
    return title.hashCode ^
        use.hashCode ^
        verb.hashCode ^
        (input == null ? 0 : const ListEquality().hash(input)) ^
        (output == null ? 0 : const ListEquality().hash(output)) ^
        (steps == null ? 0 : const ListEquality().hash(steps)) ^
        test.hashCode;
  }
}

class Step {
  // The scope expression is matched against the session just like the
  // input expressions are, but its value is not passed on to the
  // module instance, but is used to place outputs on.
  final PathExpr scope;

  // The verb is used to match the module.
  final Verb verb;

  // The input expressions are evaluated against the session and
  // determine how many instances of the module are executed.
  final List<PathExpr> input;

  // The output expressions determine what output the module instance
  // is allowed to create.
  final List<PathExpr> output;

  // The display types this step is required to be able to assume.
  final List<PathExpr> display;

  // The compose types this step is required to be able to assume.
  final List<PathExpr> compose;

  // In recipe, this url means that the step must be resolved to the module
  // served at url.
  final Uri url;

  static int _nextId = 0;
  final int id = _nextId++;

  Step(this.scope, this.verb, this.input, this.output, this.display,
      this.compose, this.url);

  /// Creates a Step from a Manifest that would match that Manifest. Used by
  /// suggestinator. This depends on how ManifestMatcher matches Steps back to
  /// Manifests. The Step contains expressions for the first segments of all
  /// expressions in the manifest, so that the recipe can supply anchors for all
  /// expressions in the manifest.
  factory Step.fromManifest(final Manifest manifest) {
    // Anchors for the data and compose inputs are the first segments of the
    // input expressions in the manifest, and the first segments of those
    // compose expressions that have more than one segment.
    final List<PathExpr> stepInput =
        _stepDataExpr(manifest.input, manifest.compose);

    // Anchors for the outputs are the first segments of the output expressions
    // in the manifest, and the first segments of those display expressions that
    // have more than one segment, if they are not already among the input
    // anchors above. TODO(mesch): Ignore cardinality expressions should really
    // be optional repeated.
    final List<PathExpr> stepOutput = _stepDataExpr(
        manifest.output, manifest.display,
        ignoreCardinality: true, exclude: stepInput);

    // Anchors for display and compose are only needed for those expressions
    // that are simple; for complex expressions the anchors are already in the
    // input and output anchors above.
    final List<PathExpr> stepCompose = _stepDisplayExpr(manifest.compose);
    final List<PathExpr> stepDisplay =
        _stepDisplayExpr(manifest.display, ignoreCardinality: true);

    final Step ret = new Step(_scopeExpr(manifest.input), manifest.verb,
        stepInput, stepOutput, stepDisplay, stepCompose, manifest.url);

    return ret;
  }

  /// HACK(mesch): We take the first singular manifest input to be the scope.
  /// The scope expression has no representation type.
  static PathExpr _scopeExpr(final List<PathExpr> exprs) {
    for (final PathExpr expr in exprs) {
      if (expr.properties.first.cardinality == Cardinality.singular) {
        return new PathExpr.single(
            new Property(expr.properties.first.labels, Cardinality.singular));
      }
    }
    return null;
  }

  static List<PathExpr> _stepDataExpr(
      final Iterable<PathExpr> dataExprs, final Iterable<PathExpr> displayExprs,
      {final bool ignoreCardinality: false, final Iterable<PathExpr> exclude}) {
    final List<PathExpr> ret = <PathExpr>[];

    final List<PathExpr> exprs = <PathExpr>[];
    exprs.addAll(dataExprs);
    exprs.addAll(displayExprs.where((PathExpr expr) => !expr.isSimple));

    for (final PathExpr expr in exprs) {
      final PathExpr anchorExpr = new PathExpr.single(ignoreCardinality
          ? new Property(expr.properties.first.labels)
          : new Property(
              expr.properties.first.labels, expr.properties.first.cardinality));
      if (!ret.contains(anchorExpr) &&
          (exclude == null || !exclude.contains(anchorExpr))) {
        ret.add(anchorExpr);
      }
    }

    return ret;
  }

  static List<PathExpr> _stepDisplayExpr(final Iterable<PathExpr> exprs,
      {final bool ignoreCardinality: false}) {
    final List<PathExpr> ret = <PathExpr>[];

    for (final PathExpr expr in exprs.where((PathExpr expr) => expr.isSimple)) {
      final PathExpr anchorExpr = new PathExpr.single(ignoreCardinality
          ? new Property(expr.properties.first.labels)
          : new Property(
              expr.properties.first.labels, expr.properties.first.cardinality));
      if (!ret.contains(anchorExpr)) {
        ret.add(anchorExpr);
      }
    }

    return ret;
  }

  factory Step.fromJson(Map<String, dynamic> values) {
    return new Step(
        values['scope'] != null ? new PathExpr.fromJson(values['scope']) : null,
        values['verb'] != null ? new Verb.fromJson(values['verb']) : null,
        values['input'].map((i) => new PathExpr.fromJson(i)).toList(),
        values['output'].map((i) => new PathExpr.fromJson(i)).toList(),
        values['display'].map((i) => new PathExpr.fromJson(i)).toList(),
        values['compose'].map((i) => new PathExpr.fromJson(i)).toList(),
        values['url'] != null ? Uri.parse(values['url']) : null);
  }

  String toJsonString() {
    return Timeline.timeSync("$runtimeType toJson", () => JSON.encode(this));
  }

  dynamic toJson() => {
        'scope': scope,
        'verb': verb,
        'input': input,
        'output': output,
        'display': display,
        'compose': compose,
        'url': url?.toString()
      };

  /// Returns true, if rhs [Step] can be replaced with the current step or
  /// vice-versa in the recipe, which can handle input and outputs similarly.
  /// Note, we don't match for urls.
  bool isFunctionallyEquivalent(Step rhs) {
    return scope == rhs.scope &&
        verb == rhs.verb &&
        const ListEquality().equals(input, rhs.input) &&
        const ListEquality().equals(output, rhs.output) &&
        const ListEquality().equals(display, rhs.display) &&
        const ListEquality().equals(compose, rhs.compose);
  }

  /// Computes the full paths from the session root through the step expression
  /// and the anchor to the given manifest expressions.
  List<List<Property>> resolvePaths(final List<PathExpr> manifestExprs) {
    final List<List<Property>> paths = <List<Property>>[];

    for (final PathExpr manifestExpr in manifestExprs) {
      final List<Property> path = <Property>[];

      final PathExpr anchor = _findAnchor(manifestExpr);
      if (anchor != null) {
        path.addAll(anchor.properties);
        path.addAll(manifestExpr.properties.sublist(1));
      } else {
        path.addAll(manifestExpr.properties);
      }

      paths.add(path);
    }

    return paths;
  }

  /// Finds the expression in step that anchors the given manifest expression.
  PathExpr _findAnchor(final PathExpr manifestExpr) {
    final List<PathExpr> stepExpressions = <PathExpr>[]
      ..addAll(input)
      ..addAll(output)
      ..addAll(compose)
      ..addAll(display);
    for (final PathExpr stepExpression in stepExpressions) {
      if (stepExpression.properties.last.labels
          .containsAll(manifestExpr.properties.first.labels)) {
        return stepExpression;
      }
    }

    return null;
  }

  @override
  bool operator ==(rhs) {
    return rhs is Step && isFunctionallyEquivalent(rhs) && url == rhs.url;
  }

  @override
  int get hashCode {
    return scope.hashCode ^
        verb.hashCode ^
        (input == null ? 0 : const ListEquality().hash(input)) ^
        (output == null ? 0 : const ListEquality().hash(output)) ^
        (display == null ? 0 : const ListEquality().hash(display)) ^
        (compose == null ? 0 : const ListEquality().hash(compose)) ^
        url.hashCode;
  }

  @override
  String toString() => "$runtimeType:\n"
      "scope: $scope\n"
      "verb: $verb\n"
      "input: $input\n"
      "output: $output\n"
      "display: $display\n"
      "compose: $compose\n"
      "url: $url\n";
}
