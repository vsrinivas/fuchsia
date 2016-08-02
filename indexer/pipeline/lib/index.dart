// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:parser/expression.dart';
import 'package:parser/manifest.dart';
import 'package:parser/parser.dart';

class RecipeEntry {
  final String name;
  final String url;

  const RecipeEntry(this.name, this.url);
}

class TypeEntry {
  int referenceCount = 0;
  Set<String> shorthands = new Set<String>();
}

class Index {
  /// The following counters map label uris to the number of different indexed
  /// manifests that reference them.
  final Map<Uri, TypeEntry> _verbCounters = <Uri, TypeEntry>{};
  final Map<Uri, TypeEntry> _semanticCounters = <Uri, TypeEntry>{};
  final Map<Uri, TypeEntry> _representationCounters = <Uri, TypeEntry>{};
  final Map<Uri, TypeEntry> _embodimentCounters = <Uri, TypeEntry>{};

  /// List of recipes.
  final List<RecipeEntry> recipes = [];

  /// List of manifests.
  final List<Manifest> manifests = [];

  /// Parses the given manifest and adds it to the index.
  void addManifest(final String manifestContent) {
    final Manifest manifest = parseManifest(manifestContent);
    _addParsedManifest(manifest);
  }

  /// Parses the given manifest file and adds its content to the index.
  Future<Null> addManifestFile(final File manifestFile) async {
    final Manifest manifest = await parseManifestFile(manifestFile.path);
    _addParsedManifest(manifest);
  }

  void _addParsedManifest(final Manifest manifest) {
    manifests.add(manifest);

    // Flat list of all path expressions in the manifest.
    final Iterable<PathExpr> ioPathExpressions = [
      manifest.input,
      manifest.output,
    ].expand((final List<PathExpr> pathExpressions) => pathExpressions);
    final Iterable<PathExpr> embodimentPathExpressions = [
      manifest.display,
      manifest.compose,
    ].expand((final List<PathExpr> pathExpressions) => pathExpressions);

    // Process verbs.
    _accountLabels(_verbCounters, [manifest.verb.label]);

    Iterable<Label> labelsFromPathExpr(Iterable<PathExpr> exprs) => exprs
        .expand((final PathExpr expr) => expr.properties)
        .expand((final Property property) => property.labels);

    // Process semantic labels.
    _accountLabels(_semanticCounters, labelsFromPathExpr(ioPathExpressions));
    _accountLabels(
        _embodimentCounters, labelsFromPathExpr(embodimentPathExpressions));

    // Process representation labels.
    _accountLabels(
        _representationCounters,
        ioPathExpressions
            .expand((final PathExpr expr) => expr.properties)
            .expand((final Property property) => property.representations));
  }

  /// Adds the given recipe to the index. Note that we currently don't index the
  /// content of a recipe.
  void addRecipe(String name, String url) {
    recipes.add(new RecipeEntry(name, url));
  }

  List<Map<String, dynamic>> get verbRanking => _rankLabels(_verbCounters);
  List<Map<String, dynamic>> get semanticRanking =>
      _rankLabels(_semanticCounters);
  List<Map<String, dynamic>> get representationRanking =>
      _rankLabels(_representationCounters);
  List<Map<String, dynamic>> get embodimentRanking =>
      _rankLabels(_embodimentCounters);

  /// Updates the given counter map to account for the given labels.
  void _accountLabels(
      final Map<Uri, TypeEntry> counters, final Iterable<Label> labels) {
    for (final Label label in labels) {
      counters.putIfAbsent(label.uri, () => new TypeEntry());
      counters[label.uri].referenceCount += 1;
      counters[label.uri].shorthands.add(label.shorthand);
    }
  }

  /// Returns ranking of labels from the given counters. The returned entries
  /// are maps of "uri" and "referenceCount", which allows to directly pass them
  /// to mustache renderer.
  List<Map<String, dynamic>> _rankLabels(final Map<Uri, TypeEntry> counters) {
    final List<Map<String, dynamic>> ranking = <Map<String, dynamic>>[];
    counters.forEach((final Uri uri, final TypeEntry entry) {
      ranking.add({
        'uri': uri.toString(),
        'referenceCount': entry.referenceCount,
        'shorthands': entry.shorthands.toList()
      });
    });

    ranking.sort((final Map<String, dynamic> entry1,
            final Map<String, dynamic> entry2) =>
        -entry1['referenceCount'].compareTo(entry2['referenceCount']));
    return ranking;
  }
}
