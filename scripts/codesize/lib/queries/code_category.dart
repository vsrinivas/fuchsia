// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

import 'dart:core';
import 'package:collection/collection.dart';

import '../common_util.dart';
import '../render/ast.dart';
import '../types.dart';
import 'categories/categories.dart';
import 'index.dart';

/// A code category is a pattern detector that looks for a particular set of
/// interesting symbols e.g. FIDL C++ coding tables. There are many code
/// categories defined, and a symbol must fall into exactly one category,
/// with `uncategorized` being a special fallback category.
abstract class CodeCategory {
  const CodeCategory();

  /// Returns if the (symbol, compileUnit) combination matches this category.
  /// Additionally, the matching is performed in two phases, `match` and
  /// `rematch`. During this `match` phase, implementations may additionally
  /// build up contextual information that will help make the second `rematch`
  /// run more accurate. For example, `HlcppDomainObjectCategory` would infer
  /// the (potentially out-of-tree) FIDL library name if applicable, and use
  /// that to match other more ambiguous HLCPP FIDL symbols.
  bool match(
      String symbol, CompileUnitContext compileUnit, ProgramContext program);

  /// Performs a more extensive match using information gathered from `match`.
  /// See `CodeCategory.match`.
  bool rematch(
      String symbol, CompileUnitContext compileUnit, ProgramContext program) {
    return false;
  }

  /// A human-readable description of what kinds of codes are covered under
  /// this category.
  String get description;

  String get name => maybeRemoveSuffix(runtimeType.toString(), 'Category');

  @override
  String toString() => name;
}

/// The signature of `CodeCategory.match` and `CodeCategory.rematch`.
typedef Matcher = bool Function(String, CompileUnitContext, ProgramContext);

/// Marker interface that all FIDL categories will implement.
class SomeFidlCategory {}

class Uncategorized extends CodeCategory {
  const Uncategorized();

  @override
  String get description => 'Symbols with a potentially indicative name, '
      'but otherwise did not fall under any of the defined code categories';

  @override
  bool match(
      String symbol, CompileUnitContext compileUnit, ProgramContext program) {
    return false;
  }
}

const uncategorized = Uncategorized();

class CodeCategoryQuery extends Query {
  static const String description =
      'Breaks down the symbols by categories and aggregates their size';

  @override
  String getDescription() => description;

  CodeCategoryQuery({this.numProgramsToShow = 5});

  final int numProgramsToShow;

  @override
  void addReport(Report report) {
    for (final compileUnit in report.compileUnits) {
      final symbolCategories = analyzeCompileUnit(compileUnit, report);

      for (final entry in symbolCategories.entries) {
        _addStats(entry.key.sizes, entry.value, report.context.name);
      }
    }
  }

  Map<Symbol, Set<CodeCategory>> analyzeCompileUnit(
      CompileUnit compileUnit, Report report) {
    final symbolCategories = <Symbol, Set<CodeCategory>>{};
    if (compileUnit.symbols == null) return symbolCategories;

    /// Match the symbol against the match functions from all the categories.
    Set<CodeCategory> matchCategories(Symbol symbol, CompileUnit compileUnit,
        Report report, Matcher Function(CodeCategory) toMatchFunction) {
      final matchedCategories = <CodeCategory>{};
      for (final category in _compatibleCategories) {
        if (toMatchFunction(category)(
            symbol.name, compileUnit.context, report.context)) {
          matchedCategories.add(category);
        }
      }

      // Special safeguard for exclusive code category rules: a symbol cannot
      // simultaneously belong to two exclusive code categories. This guards
      // against the categories themselves being too permissive.
      CodeCategory firstExclusiveCategory;
      for (final category in _exclusiveCategories) {
        if (toMatchFunction(category)(
            symbol.name, compileUnit.context, report.context)) {
          if (firstExclusiveCategory != null) {
            throw Exception('More than one exclusive category in '
                '${report.context.name}.\n'
                'Symbol: ${symbol.name}\n'
                'was both $firstExclusiveCategory and $category.\n'
                'Compile unit: ${compileUnit.name}\n');
          }
          firstExclusiveCategory = category;
          matchedCategories.add(category);
        }
      }
      return matchedCategories;
    }

    // First pass
    for (final symbol in compileUnit.symbols) {
      symbolCategories.putIfAbsent(symbol, () => <CodeCategory>{});
      symbolCategories[symbol].addAll(matchCategories(
          symbol, compileUnit, report, (category) => category.match));
    }

    // Second pass.
    // During the second pass, we only allow the category of a symbol to go from
    // "no categories" to some meaningful category, or stay in the same
    // category. Switching categories during `rematch` indicates potentially
    // buggy category matches, hence we noisily fail in that case.
    for (final symbol in compileUnit.symbols) {
      final matchedCategories = symbolCategories[symbol];
      final rematchedCategories = matchCategories(
          symbol, compileUnit, report, (category) => category.rematch);
      if (matchedCategories.isEmpty) {
        symbolCategories[symbol] = rematchedCategories;
      } else if (rematchedCategories.isNotEmpty &&
          !_compareCategories.equals(matchedCategories, rematchedCategories)) {
        throw Exception('${symbol.name} went from $matchedCategories '
            'to $rematchedCategories during rematch');
      }
      if (symbolCategories[symbol].isEmpty) {
        symbolCategories[symbol] = <CodeCategory>{uncategorized};
      }
    }

    return symbolCategories;
  }

  @override
  void mergeWith(Iterable<Query> others) {
    for (final other in others) {
      if (other is CodeCategoryQuery) {
        for (final entry in other._tallies.entries) {
          _tallies[entry.key] += entry.value;
        }
        for (final entry in other._statsByBinary.entries) {
          mergeMapInto(_statsByBinary[entry.key], entry.value);
        }
      } else {
        throw Exception('$other must be $runtimeType');
      }
    }
  }

  void _addStats(
      SizeInfo sizes, Set<CodeCategory> categories, String binaryName) {
    for (final category in categories) {
      _tallies[category]
        ..size += sizes.fileActual
        ..count += 1;
      _statsByBinary[category].putIfAbsent(binaryName, Tally.zero)
        ..size += sizes.fileActual
        ..count += 1;
    }
  }

  /// Code categories that are mutually exclusive with one another.
  /// A symbol may only match up to one of these categories.
  static const List<CodeCategory> _exclusiveCategories = [
    CppCodingTableCategory(),
    HlcppDomainObjectCategory(),
    LlcppDomainObjectCategory(),
    HlcppRuntimeCategory(),
    LlcppRuntimeCategory(),
    CFidlCategory(),
    GoFidlCategory(),
    RustFidlCategory(),
    UntraceableCategory(),
  ];

  /// Code categories that are compatible with one another, and with the
  /// exclusive categories. A symbol may match any number of these categories.
  static const List<CodeCategory> _compatibleCategories = [];

  /// List of `CodeCategory`s checked by the query.
  static const List<CodeCategory> _allCategories = [
    ..._exclusiveCategories,
    ..._compatibleCategories
  ];

  final _tallies = Map<CodeCategory, Tally>.fromEntries(
      (_allCategories + [uncategorized]).map((s) => MapEntry(s, Tally.zero())));
  final _statsByBinary = Map<CodeCategory, Map<String, Tally>>.fromEntries(
      (_allCategories + [uncategorized]).map((s) => MapEntry(s, {})));

  static const _compareCategories = SetEquality<CodeCategory>();

  @override
  String toString() {
    final sortedBySize = _tallies.keys.toList()
      ..sort((a, b) => _tallies[a].size.compareTo(_tallies[b].size));
    return sortedBySize.reversed.map((k) => ' - $k: ${_tallies[k]}').join('\n');
  }

  @override
  QueryReport distill() => CodeCategoryReport(_tallies, _statsByBinary,
      numProgramsToShow: numProgramsToShow);
}

class CodeCategoryReport implements QueryReport {
  CodeCategoryReport(this._tallies, this._statsByBinary,
      {this.numProgramsToShow}) {
    _sortedBySize = _tallies.keys.toList()
      ..sort((a, b) => -_tallies[a].size.compareTo(_tallies[b].size));
    for (final k in _sortedBySize) {
      _statistics[k] = Statistics(_statsByBinary[k].values);
      _sortedBinariesPerCategory[k] = sortBinaries(_statsByBinary[k]);
    }

    bool entryIsCppFamilyFidl<T>(MapEntry<CodeCategory, T> entry) =>
        entry.key is CppCodingTableCategory ||
        entry.key is LlcppRuntimeCategory ||
        entry.key is LlcppDomainObjectCategory ||
        entry.key is HlcppRuntimeCategory ||
        entry.key is HlcppDomainObjectCategory ||
        entry.key is CFidlCategory;

    final cppFidlStatsByBinary = _statsByBinary.entries
        .where(entryIsCppFamilyFidl)
        .map((e) => e.value)
        .fold<Map<String, Tally>>({}, mergeMapInto);
    _cppFidlStatistics =
        Statistics(cppFidlStatsByBinary.entries.map((e) => e.value));
    _cppFidlSortedBinaries = sortBinaries(cppFidlStatsByBinary);

    final allFidlStatsByBinary = _statsByBinary.entries
        .where((e) => e.key is SomeFidlCategory)
        .map((e) => e.value)
        .fold<Map<String, Tally>>({}, mergeMapInto);
    _allFidlStatistics =
        Statistics(allFidlStatsByBinary.entries.map((e) => e.value));
    _allFidlSortedBinaries = sortBinaries(allFidlStatsByBinary);
  }

  Node<StyledString> _printCategory(
      Statistics statistics, List<MapEntry<String, Tally>> sortedBinaries) {
    return Node(
        title: StyledString([
          Plain('Detected in ${statistics.count} binaries. '),
          Plain('avg: ${formatSize(statistics.mean.round())} '),
          Plain(
              '(${formatSize(statistics.min)} to ${formatSize(statistics.max)}), '),
          Plain('stdev: ${formatSize(statistics.stdev.round())}.'),
          if (sortedBinaries != null) Plain(' Top appearance:'),
        ]),
        children: sortedBinaries
            .take(numProgramsToShow)
            .map((e) => Node.plain(
                '${stripReportSuffix(e.key)} => ${formatSize(e.value.size)}'))
            .toList());
  }

  @override
  Iterable<AnyNode> export() {
    return <AnyNode>[
      // Print all the code categories first.
      for (final k in _sortedBySize)
        Node(
            title: SizeRecord(
                name: AddColor.green(Plain(k.toString())), tally: _tallies[k]),
            children: [
              Node.plain(k.description),
              if (k != uncategorized)
                _printCategory(_statistics[k], _sortedBinariesPerCategory[k])
            ]),
      // Print some higher-level synthesized information.
      Node(
          title: SizeRecord(
              name: AddColor.white(Plain('Combined C/C++ Family of FIDL code')),
              tally: _cppFidlStatistics.sum),
          children: [
            _printCategory(_cppFidlStatistics, _cppFidlSortedBinaries)
          ]),
      Node(
          title: SizeRecord(
              name: AddColor.white(Plain('Combined all FIDL code')),
              tally: _allFidlStatistics.sum),
          children: [
            _printCategory(_allFidlStatistics, _allFidlSortedBinaries)
          ]),
      Node(
          title: AddColor.white(Plain('Binaries without FIDL code:')),
          children: [
            Node(children: (() {
              final binariesWithFidl =
                  _allFidlSortedBinaries.map((e) => e.key).toSet();
              final binariesWithNonFidl = _sortedBinariesPerCategory.entries
                  .where((e) => e.key is! SomeFidlCategory)
                  .map((e) => e.value.map((e) => e.key).toSet())
                  .fold<Set<String>>({},
                      (previousValue, element) => previousValue.union(element));
              final difference = binariesWithNonFidl
                  .difference(binariesWithFidl)
                  .toList()
                    ..sort();
              return <AnyNode>[
                for (final bin in difference.take(numProgramsToShow))
                  Node.plain(bin),
                if (difference.length > numProgramsToShow)
                  Node.plain('... ${difference.length - 5} more element(s) ...')
              ];
            })())
          ]),
    ];
  }

  final int numProgramsToShow;

  final Map<CodeCategory, Tally> _tallies;
  final Map<CodeCategory, Map<String, Tally>> _statsByBinary;
  final _statistics = <CodeCategory, Statistics>{};
  final _sortedBinariesPerCategory =
      <CodeCategory, List<MapEntry<String, Tally>>>{};
  List<CodeCategory> _sortedBySize;
  Statistics _cppFidlStatistics;
  List<MapEntry<String, Tally>> _cppFidlSortedBinaries;
  Statistics _allFidlStatistics;
  List<MapEntry<String, Tally>> _allFidlSortedBinaries;
}

String stripReportSuffix(String name) {
  if (name.endsWith('.bloaty_report_pb'))
    return name.substring(0, name.length - 18);
  return name;
}
