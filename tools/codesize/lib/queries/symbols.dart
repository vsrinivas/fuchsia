// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

import 'dart:core';
import 'dart:typed_data';

import '../common_util.dart';
import '../render/ast.dart';
import '../types.dart';
import 'categories/untraceable.dart';
import 'code_category.dart';
import 'index.dart';

class SymbolsQuery extends Query implements QueryReport {
  static const String description =
      'Presents a hierarchical view of the symbols, '
      'grouped by compile units. '
      'Unlike UniqueSymbol, this query does not aggregate them together, '
      'and shows the biggest compile units (roughly source files) first.';

  @override
  String getDescription() => description;

  /// Constructs a symbols query. A `limit` of 0 indicates no limit on the
  /// number of rows of symbols in the output.
  SymbolsQuery({this.hideUnknown = false, int limit = defaultSymbolCountLimit})
      : _limit = limit == 0 ? null : limit;

  @override
  void addReport(Report report) {
    _serializedReports[report.context.name] = report.toBloaty().writeToBuffer();
  }

  @override
  void mergeWith(Iterable<Query> others) {
    for (final other in others) {
      if (other is SymbolsQuery) {
        for (final entry in other._serializedReports.entries) {
          if (_serializedReports.containsKey(entry.key)) {
            throw Exception('Merging bloaty reports is unsupported');
          }
          _serializedReports[entry.key] = entry.value;
        }
      } else {
        throw Exception('$other must be $runtimeType');
      }
    }
  }

  @override
  QueryReport distill() => this;

  /// Storing a map of binary names to their serialized protobuf reports,
  /// because the Dart VM currently has some issues passing the complex
  /// `Report` type directly across isolates.
  final _serializedReports = <String, Uint8List>{};

  @override
  String toString() {
    if (_serializedReports.entries.isEmpty) {
      return ' - Nothing selected';
    }
    final buffer = StringBuffer();
    for (final entry in _serializedReports.entries) {
      buffer.writeln(' - ${entry.key}');
      final report = Report.fromBytes(name, entry.value);
      for (final compileUnit in report.compileUnits) {
        for (final symbol in compileUnit.symbols) {
          buffer.writeln('   # ${symbol.name}');
        }
      }
    }
    return buffer.toString();
  }

  @override
  Iterable<AnyNode> export() {
    if (_serializedReports.entries.isEmpty) {
      return [Node.plain('Nothing selected')];
    }
    final reports =
        _serializedReports.entries.map((e) => Report.fromBytes(e.key, e.value));
    if (_serializedReports.length == 1) {
      // Flatten out the "program" level.
      return reports.expand(_exportSingleReport);
    } else {
      // The top level will be the various programs (reports).
      return reports.map((report) => Node(
          title: SizeRecord(
              name: AddColor.white(Plain(report.context.name)),
              tally: Tally(report.fileTotal, 1)),
          children: _exportSingleReport(report).toList()));
    }
  }

  Iterable<AnyNode> _exportSingleReport(Report report) {
    final codeCategory = CodeCategoryQuery();
    return report.compileUnits.map((compileUnit) {
      final symbolCategories =
          codeCategory.analyzeCompileUnit(compileUnit, report);

      return Node(
          title: StyledString([
            AddColor.white(Plain(compileUnit.name)),
            Plain(' (${formatSize(compileUnit.sizes.fileActual)})'),
            Plain(', in ELF '),
            AddColor.white(Plain(report.context.name)),
            Plain(': '),
          ]),
          children: (() {
            List<AnyNode> nodes = [];
            bool isUnknown(CodeCategory category) =>
                category is Uncategorized || category is UntraceableCategory;
            Iterable<Symbol> symbolsToPrint;
            if (hideUnknown) {
              final unknownSymbols = compileUnit.symbols
                  .where((s) => symbolCategories[s].every(isUnknown));
              symbolsToPrint = compileUnit.symbols
                  .where((s) => !symbolCategories[s].every(isUnknown));
              final unknownSize = unknownSymbols
                  .map((e) => e.sizes.fileActual)
                  .fold<int>(0, (value, element) => value + element);
              if (unknownSize > 0) {
                nodes.add(Node(
                    title: AddColor(
                        Color.gray,
                        Plain('... ${unknownSymbols.length} '
                            'uncategorized/untraceable symbols omitted '
                            '(${formatSize(unknownSize)}) ...'))));
              }
            } else {
              symbolsToPrint = compileUnit.symbols;
            }
            final Iterable<Symbol> truncated =
                _limit == null ? symbolsToPrint : symbolsToPrint.take(_limit);
            for (final symbol in truncated) {
              nodes.add(Node(
                  title: StyledString([
                Plain('${symbol.name} '),
                for (final category in symbolCategories[symbol])
                  AddColor(isUnknown(category) ? Color.gray : Color.green,
                      Plain('[${category.toString()}] ')),
                Plain('(${formatSize(symbol.sizes.fileActual)})'),
              ])));
            }
            if (_limit != null && symbolsToPrint.length > _limit) {
              nodes.add(Node(
                  title: AddColor(
                      Color.gray,
                      Plain('... ${symbolsToPrint.length - _limit} '
                          'more symbols omitted due to limit ...'))));
            }
            return nodes;
          })());
    });
  }

  /// Whether to hide symbols which do not fall into any meaningful
  /// code category.
  final bool hideUnknown;

  /// How many rows of symbols to display, before the output is clipped, to aid
  /// readability.
  final int _limit;

  static const int defaultSymbolCountLimit = 15;
}
