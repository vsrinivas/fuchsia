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
import 'dump_names.dart';
import 'source_lang.dart';

class CompileUnitContext with SourceLangCompileContextMixin {
  CompileUnitContext(this.name);
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

List<MapEntry<String, Tally>> sortBinaries(Map<String, Tally> tallyByBinary) =>
    tallyByBinary.entries.toList()
      ..sort((a, b) => ((int c) => c != 0 ? c : a.key.compareTo(b.key))(
          -a.value.size.compareTo(b.value.size)));

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

  static String stripQuerySuffix(String typeName) => typeName.endsWith('Query')
      ? typeName.substring(0, typeName.length - 'Query'.length)
      : typeName;
}

/// A `QueryReport` contains the distilled results from running a query,
/// ready to be exported into some output format.
///
// We may add more methods in the future.
// ignore: one_member_abstracts
abstract class QueryReport {
  Iterable<AnyNode> export();
}

class QueryFactory {
  const QueryFactory(this.type, this.description);
  final Type type;
  final String description;

  String get name => Query.stripQuerySuffix(type.toString());
}

const List<QueryFactory> allQueries = [
  QueryFactory(SourceLangQuery, SourceLangQuery.description),
  QueryFactory(DumpNamesQuery, DumpNamesQuery.description),
];

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
