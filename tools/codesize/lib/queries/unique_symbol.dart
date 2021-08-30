// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

import 'dart:collection';
import 'dart:core';

import '../render/ast.dart';
import '../types.dart';
import 'categories/untraceable.dart';
import 'code_category.dart';
import 'index.dart';

class SymbolMetadata {
  SymbolMetadata(this.name, this.tally, this.categories, this.compileUnits,
      this.programs, this.rustCrates);

  factory SymbolMetadata.initial(String name) => SymbolMetadata(
      name, Tally.zero(), <CodeCategory>{}, <String>{}, <String>{}, <String>{});

  SymbolMetadata mergeWith(SymbolMetadata other) {
    tally += other.tally;
    categories.addAll(other.categories);
    compileUnits.addAll(other.compileUnits);
    programs.addAll(other.programs);
    rustCrates.addAll(other.rustCrates);
    return this;
  }

  final String name;
  Tally tally;
  Set<CodeCategory> categories;
  Set<String> compileUnits;
  Set<String> programs;
  Set<String> rustCrates;
}

/// Aggregates unique symbols and associated metadata. Specifically,
/// collects their containing compile unit names, ELF binary names,
/// and `CodeCategoryQuery` annotations.
class UniqueSymbolQuery extends Query {
  static const String description =
      'Displays sorted unique symbols in binaries and '
      'aggregates their size and code categories.';

  @override
  String getDescription() => description;

  UniqueSymbolQuery(
      {this.showCompileUnit = false,
      this.showProgram = false,
      this.hideUnknown = true,
      this.onlyCategory = ''});

  final bool showCompileUnit;

  final bool showProgram;

  final bool hideUnknown;

  final String onlyCategory;

  final _codeCategory = CodeCategoryQuery();

  @override
  void addReport(Report report) {
    if (report.compileUnits == null)
      throw Exception('Error loading compile units');

    for (final compileUnit in report.compileUnits) {
      final symbolCategories =
          _codeCategory.analyzeCompileUnit(compileUnit, report);
      for (final symbol in compileUnit.symbols) {
        final category = symbolCategories[symbol];
        _stats.putIfAbsent(
            symbol.name, () => SymbolMetadata.initial(symbol.name));
        _stats[symbol.name]
          ..tally += Tally(symbol.sizes.fileActual, 1)
          ..categories.addAll(category)
          ..compileUnits.add(compileUnit.name)
          ..programs.add(report.context.name);
        if (symbol.maybeRustCrate != null && symbol.maybeRustCrate.isNotEmpty) {
          _stats[symbol.name].rustCrates.add(symbol.maybeRustCrate);
        }
      }
    }
  }

  @override
  void mergeWith(Iterable<Query> others) {
    for (final other in others) {
      if (other is UniqueSymbolQuery) {
        for (final entry in other._stats.entries) {
          _stats
              .putIfAbsent(entry.key, () => SymbolMetadata.initial(entry.key))
              .mergeWith(entry.value);
        }
      } else {
        throw Exception('$other must be $runtimeType');
      }
    }
  }

  @override
  QueryReport distill() => UniqueSymbolReport(_stats,
      showCompileUnit: showCompileUnit,
      showProgram: showProgram,
      hideUnknown: hideUnknown,
      onlyCategory: onlyCategory);

  final _stats = <String, SymbolMetadata>{};

  @override
  String toString() {
    if (_stats.entries.isEmpty) {
      return 'Nothing selected';
    }
    return _stats.entries.map((e) => '${e.key}').join('\n');
  }
}

class UniqueSymbolReport implements QueryReport {
  UniqueSymbolReport(Map<String, SymbolMetadata> stats,
      {this.showCompileUnit,
      this.showProgram,
      this.hideUnknown,
      this.onlyCategory}) {
    _stats =
        SplayTreeMap<String, SymbolMetadata>.of(stats, (String k1, String k2) {
      final compareTally = -stats[k1].tally.compareTo(stats[k2].tally);
      if (compareTally != 0) return compareTally;
      return k1.compareTo(k2);
    });
  }

  @override
  Iterable<AnyNode> export() {
    if (_stats.entries.isEmpty) {
      return [Node.plain('Nothing selected')];
    }
    return _stats.entries.expand((entry) {
      final name = entry.key;
      final symbol = entry.value;

      bool isUnknown(CodeCategory category) =>
          category is Uncategorized || category is UntraceableCategory;
      final categories = <StyledString>[];
      var allUnknown = true;
      // Whether we have encountered the code category specified
      // in `onlyCategory`.
      var foundOnlyCategory = false;
      for (final category in symbol.categories) {
        Color color;
        if (isUnknown(category)) {
          color = Color.gray;
        } else {
          color = Color.green;
          allUnknown = false;
        }
        if (onlyCategory.isNotEmpty && category.name == onlyCategory) {
          foundOnlyCategory = true;
        }
        final categoryLabel = AddColor(color, Plain(category.toString()));
        categories.add(categoryLabel);
      }
      if (allUnknown && hideUnknown) return [];
      if (onlyCategory.isNotEmpty && !foundOnlyCategory) return [];

      return [
        Node(
            title: UniqueSymbolSizeRecord(
                name: AddColor.white(Plain(name)),
                tally: symbol.tally,
                categories: categories,
                rustCrates: symbol.rustCrates
                    .map((e) => StyledString.plain(e))
                    .toList()),
            children: [
              if (showCompileUnit)
                Node(title: StyledString.plain('In compile unit:'), children: [
                  for (final compileUnit in symbol.compileUnits.take(5))
                    Node.plain(compileUnit),
                  if (symbol.compileUnits.length > 5)
                    Node.plain(
                        '... ${symbol.compileUnits.length - 5} more ...'),
                ]),
              if (showProgram)
                Node(title: StyledString.plain('In binaries:'), children: [
                  for (final program in symbol.programs.take(5))
                    Node.plain(program),
                  if (symbol.programs.length > 5)
                    Node.plain('... ${symbol.programs.length - 5} more ...'),
                ])
            ])
      ];
    });
  }

  final bool showCompileUnit;

  final bool showProgram;

  final bool hideUnknown;

  final String onlyCategory;

  SplayTreeMap<String, SymbolMetadata> _stats;
}
