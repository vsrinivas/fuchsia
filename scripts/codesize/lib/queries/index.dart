// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

/// The queries library, containing size analytics modules for bloaty reports.
library queries;

import 'dart:core';
import 'dart:core' as core;
import 'dart:math' as math;

import '../common_util.dart';
import '../render/ast.dart';
import '../types.dart';
import 'categories/categories.dart';
import 'code_category.dart';
import 'crates.dart';
import 'dump_names.dart';
import 'source_lang.dart';
import 'unique_symbol.dart';

/// A context object that allows queries to temporarily stow information
/// related to a particular compile unit, at the same time ensuring separation
/// of concerns.
///
/// This is accomplished by having query implementations define their
/// domain-specific mixins, which are combined together in this class.
///
/// The mixins themselves typically define lazily evaluated computations for
/// fields that are costly to compute. For example, many queries may make use of
/// the source langauge information of a compile unit, and that information is
/// defined and evaluated at most once, in `SourceLangCompileContextMixin`.
///
/// ## Lifecycle of a context
///
/// When running codesize on reports, the `CompileUnitContext` is created once
/// for each compile unit that appears in a bloaty report, and then re-used for
/// all queries. Afterwards, it is discarded and any information stored related
/// to this compile unit are lost.
class CompileUnitContext
    with
        SourceLangCompileContextMixin,
        CppCodingTableContextMixin,
        CppDomainObjectContextMixin,
        CppRuntimeContextMixin,
        CFidlContextMixin,
        GoFidlContextMixin,
        RustFidlContextMixin,
        UntraceableContextMixin {
  CompileUnitContext(this.name);

  /// Name of the compile unit.
  final String name;
}

class ProgramContext with SourceLangContextMixin {
  ProgramContext(this.name, Report report) {
    initSourceLangContextMixin(report);
  }
  final String name;
}

/// The total `size` and `count` information of some entity being aggregated:
/// symbols, compile units, binaries, etc.
/// This is a simple data class that supports addition and comparison.
class Tally implements Comparable {
  Tally(this.size, this.count);

  /// How many bytes in total was occupied by the entity.
  int size;

  /// How many times was this entity observed.
  int count;

  /// Tear-off accessor for the `size` field.
  static int toSize(Tally t) => t.size;

  /// Tear-off accessor for `count` field.
  static int toCount(Tally t) => t.count;

  // Static methods could be used as a tear-off.
  // ignore: prefer_constructors_over_static_methods
  static Tally zero() => Tally(0, 0);

  @override
  int compareTo(dynamic other) {
    if (other is Tally) {
      final compSize = size.compareTo(other.size);
      if (compSize != 0) {
        return compSize;
      }
      return count.compareTo(other.count);
    } else {
      throw ArgumentError('$other cannot be compared');
    }
  }

  Tally operator +(Tally other) {
    return Tally(size + other.size, count + other.count);
  }

  @override
  bool operator ==(Object other) =>
      other is Tally && other.size == size && other.count == count;

  @override
  int get hashCode => size.hashCode ^ count.hashCode;

  Tally mergeWith(Tally other) {
    size += other.size;
    count += other.count;
    return this;
  }

  @override
  String toString() => '${formatSize(size)} ($size), count: $count';
}

/// Merge the contents of `b` into `a`.
Map<T, Tally> mergeMapInto<T>(Map<T, Tally> a, Map<T, Tally> b) {
  for (final entry in b.entries) {
    a.putIfAbsent(entry.key, Tally.zero).mergeWith(entry.value);
  }
  return a;
}

String printMapSorted<K>(Map<K, Tally> map) {
  final sortedBySize = map.keys.toList()
    ..sort((a, b) => map[a].size.compareTo(map[b].size));
  return sortedBySize.reversed.map((k) {
    return ' - $k: ${map[k]}';
  }).join('\n');
}

/// Sorts a map of binary names to their size, breaking ties by first sorting
/// by size, then sorting by name lexicographically.
List<MapEntry<String, Tally>> sortBinaries(Map<String, Tally> tallyByBinary) =>
    tallyByBinary.entries.toList()
      ..sort((a, b) => ((int sizeComp) => sizeComp != 0
          ? sizeComp
          : a.key.compareTo(b.key))(-a.value.size.compareTo(b.value.size)));

/// Utility class for calculating various statistics on a set of tallies.
class Statistics {
  Statistics(Iterable<Tally> elements) {
    if (elements.isEmpty) {
      sum = Tally.zero();
      count = 0;
      mean = 0;
      stdev = 0;
      min = 0;
      max = 0;
      return;
    }
    Iterable<int> sizes = elements.map(Tally.toSize);
    sum = elements.reduce((value, element) => value + element);
    count = sizes.length;
    mean = sum.size / count.toDouble();
    stdev = math.sqrt(sizes
            .map((e) => math.pow(e.toDouble() - mean, 2))
            .reduce((value, element) => value + element) /
        sizes.length.toDouble());
    min = sizes.reduce(math.min);
    max = sizes.reduce(math.max);
  }

  Tally sum;
  int count;
  double mean;
  double stdev;
  int min;
  int max;
}

/// A `Query` is some analysis that can run over all bloaty reports.
/// Queries may aggregate some statistics over the symbols in the binaries.
abstract class Query {
  /// Record the statistics from a `Report` created from a binary.
  void addReport(Report report);

  /// Combine the aggregate statistics with another instance of the same query.
  void mergeWith(Iterable<Query> others);

  /// Perform any expensive calculation and store the result in a `QueryReport`.
  /// Simple query implementations may inherit from both `Query` and
  /// `QueryReport` in the same class, in which case this method can simply
  /// return `this`.
  QueryReport distill();

  /// The name of this query.
  /// Every implementation must end with the suffix "Query", e.g. `FooBarQuery`.
  /// The name would then be `FooBar`, stripping the suffix.
  String get name => stripQuerySuffix(runtimeType.toString());

  /// Human-readable description of what this query measures.
  String getDescription();

  static String stripQuerySuffix(String typeName) =>
      maybeRemoveSuffix(typeName, 'Query');
}

/// Indicates this query should get the unfiltered bloaty report even when a
/// heatmap filter is specified.
///
/// When generating bloaty reports containing symbols and compile units, a
/// frame-based filter may be specified to only output symbols within a certain
/// byte range of the binary. This allows for example only analyzing symbols
/// that were not paged-in at runtime. However, certain queries may still wish
/// to receive the unfiltered set of symbols and compile units, because their
/// definition is independent from the access frequency e.g. [SourceLangQuery].
/// In that case, the query should implement this marker interface.
class IgnorePageInHeatmapFilter {}

/// A `QueryReport` contains the distilled results from running a query,
/// ready to be exported into some output format.
///
// We may add more methods in the future.
// ignore: one_member_abstracts
abstract class QueryReport {
  Iterable<AnyNode> export();
}

class Lazy<T, Context> {
  Lazy(this.func);
  T Function(Context self) func;
  T result;
  T call(dynamic self) {
    if (result != null) return result;
    return result = func(self);
  }
}

bool matchRegexEnsureAtMostOne(String name, List<RegExp> regex) {
  var matched = false;
  for (final regex in regex) {
    if (regex.hasMatch(name)) {
      if (matched) {
        throw Exception('Multiple matches on $name');
      }
      matched = true;
    }
  }

  return matched;
}

bool matchRegexAny(String name, List<RegExp> regex) {
  for (final regex in regex) {
    if (regex.hasMatch(name)) {
      return true;
    }
  }

  return false;
}

/// A `Renderer` is something that takes a list of queries, and prints their
/// results in a suitable format.
///
// TODO(fxbug.dev/57436): It is cleaner for renderers to take an iterable of
// `QueryReport`s, rather than `Query`s.
//
// We may add more methods in the future.
// ignore: one_member_abstracts
abstract class Renderer {
  void render(StringSink output, Iterable<Query> queries);
}

class BasicRenderer extends Renderer {
  @override
  void render(StringSink output, Iterable<Query> queries) {
    for (final query in queries) {
      output..writeln('${query.name}:')..writeln(query.toString());
    }
  }
}

/// `QueryFactory` encapsulates a query type and its description.
/// We may instantiate a query from its factory at run-time via reflection,
/// see `ReflectQuery.instantiate`.
class QueryFactory {
  const QueryFactory(this.type, this.description);
  final Type type;
  final String description;

  String get name => Query.stripQuerySuffix(type.toString());
}

/// List of all queries supported by codesize.
/// Add new queries to this list.
const List<QueryFactory> allQueries = [
  QueryFactory(CodeCategoryQuery, CodeCategoryQuery.description),
  QueryFactory(SourceLangQuery, SourceLangQuery.description),
  QueryFactory(DumpNamesQuery, DumpNamesQuery.description),
  QueryFactory(CratesQuery, CratesQuery.description),
  QueryFactory(UniqueSymbolQuery, UniqueSymbolQuery.description),
];
