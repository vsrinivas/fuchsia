// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
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

  /// Map of recipes in the index, where the Uri uniquely identifies the recipe.
  final Map<Uri, RecipeEntry> recipes = {};

  /// Map of manifests in the index, where the Uri uniquely identifies the
  /// manifest.
  final Map<Uri, Manifest> manifests = {};

  /// Parses the given [jsonIndex] string, as produced by the methods in
  /// //indexer/pipeline/render_json.dart, and adds its manifests to the index.
  void addJsonIndex(final String jsonIndex) {
    final List<Manifest> manifests =
        JSON.decode(jsonIndex).map((i) => new Manifest.fromJson(i)).toList();
    manifests.forEach((Manifest manifest) => addParsedManifest(manifest));
  }

  /// Parses the given manifest and adds it to the index.
  void addManifest(final String manifestContent) {
    final Manifest manifest = parseManifest(manifestContent);
    addParsedManifest(manifest);
  }

  /// Parses the given manifest file and adds its content to the index.
  Future<Null> addManifestFile(final File manifestFile) async {
    final Manifest manifest = await parseManifestFile(manifestFile.path);
    addParsedManifest(manifest);
  }

  /// Removes the given manifest from the index.
  void removeManifest(final String url) {
    final Uri uri = Uri.parse(url);
    if (manifests.containsKey(uri)) {
      final Manifest manifest = manifests[uri];
      _updateCounters(_forgetLabels, manifest);
      manifests.remove(uri);
    }
  }

  void addParsedManifest(final Manifest manifest) {
    // Before adding a manifest, remove any existing manifest and its label
    // counts from the index.
    removeManifest(manifest.url.toString());
    manifests[manifest.url] = manifest;
    _updateCounters(_accountLabels, manifest);
  }

  /// Updates the counter maps with the provided [manifest] using the
  /// [labelHandler]. The [labelHandler] takes an individual counter map and a
  /// list of [Label]s, updating the reference counts and shorthands
  /// accordingly.
  void _updateCounters(
      void labelHandler(
          final Map<Uri, TypeEntry> counter, final Iterable<Label> labels),
      final Manifest manifest) {
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
    labelHandler(_verbCounters, [manifest.verb.label]);

    Iterable<Label> labelsFromPathExpr(Iterable<PathExpr> exprs) => exprs
        .expand((final PathExpr expr) => expr.properties)
        .expand((final Property property) => property.labels);

    // Process semantic labels.
    labelHandler(_semanticCounters, labelsFromPathExpr(ioPathExpressions));
    labelHandler(
        _embodimentCounters, labelsFromPathExpr(embodimentPathExpressions));

    // Process representation labels.
    labelHandler(
        _representationCounters,
        ioPathExpressions
            .expand((final PathExpr expr) => expr.properties)
            .expand((final Property property) => property.representations));
  }

  /// Adds the given recipe to the index. Note that we currently don't index the
  /// content of a recipe.
  void addRecipe(String name, String url) {
    recipes[Uri.parse(url)] = new RecipeEntry(name, url);
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

  /// Updates the given counter map to neglect the given labels.
  void _forgetLabels(
      final Map<Uri, TypeEntry> counters, final Iterable<Label> labels) {
    for (final Label label in labels) {
      if (!counters.containsKey(label.uri)) {
        continue;
      }

      // Best-effort attempt at forgetting the label. Notice that we do not
      // remove shorthands as these may be non-unique across manifest files.
      counters[label.uri].referenceCount -= 1;
      if (counters[label.uri].referenceCount == 0) {
        counters.remove(label.uri);
      }
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
