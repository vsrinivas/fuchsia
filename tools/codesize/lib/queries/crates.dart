// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

import 'dart:collection';
import 'dart:core';

import '../common_util.dart';
import '../render/ast.dart';
import '../types.dart';
import 'index.dart';

class CrateMetadata {
  CrateMetadata(this.name, this.tally, this.programs);

  factory CrateMetadata.initial(String name) =>
      CrateMetadata(name, Tally.zero(), <String, Tally>{});

  CrateMetadata mergeWith(CrateMetadata other) {
    tally += other.tally;
    mergeMapInto(programs, other.programs);
    return this;
  }

  /// Name of the crate.
  final String name;

  /// Size information for this crate.
  /// It should be equal to the sum of all tallies in `programs`.
  Tally tally;

  /// Size information for this crate broken down by programs that
  /// use the crate. The map key is the pretty-printed path of an ELF binary.
  Map<String, Tally> programs;
}

/// Custom crate size record for output rendering.
class CratesSizeRecord extends SizeRecord {
  final int numPrograms;

  CratesSizeRecord({StyledString name, Tally tally, this.numPrograms})
      : super(name: name, tally: tally);
}

/// Aggregates Rust crates and associated metadata.
class CratesQuery extends Query {
  static const String description =
      'Dumps crates in Rust binaries and aggregates their size.';

  @override
  String getDescription() => description;

  CratesQuery({this.showProgram = false});

  /// When true, show the ELF binaries containing each individual crates
  /// when printing outputs.
  final bool showProgram;

  @override
  void addReport(Report report) {
    if (report.compileUnits == null)
      throw Exception('Error loading compile units');

    for (final compileUnit in report.compileUnits) {
      for (final symbol in compileUnit.symbols) {
        if (symbol.maybeRustCrate != null && symbol.maybeRustCrate.isNotEmpty) {
          _crates.putIfAbsent(symbol.maybeRustCrate,
              () => CrateMetadata.initial(symbol.maybeRustCrate));
          _crates[symbol.maybeRustCrate]
            ..tally += Tally(symbol.sizes.fileActual, 1)
            ..programs
                .putIfAbsent(report.context.name, Tally.zero)
                .mergeWith(Tally(symbol.sizes.fileActual, 1));
        }
      }
    }
  }

  @override
  void mergeWith(Iterable<Query> others) {
    for (final other in others) {
      if (other is CratesQuery) {
        for (final entry in other._crates.entries) {
          _crates
              .putIfAbsent(entry.key, () => CrateMetadata.initial(entry.key))
              .mergeWith(entry.value);
        }
      } else {
        throw Exception('$other must be $runtimeType');
      }
    }
  }

  @override
  QueryReport distill() => CratesReport(_crates, showProgram: showProgram);

  /// Mapping from crate name to crate details.
  final _crates = <String, CrateMetadata>{};

  @override
  String toString() {
    if (_crates.entries.isEmpty) {
      return 'Nothing selected';
    }
    return _crates.entries.map((e) => '${e.key}: ${e.value.tally}').join('\n');
  }
}

class CratesReport implements QueryReport {
  CratesReport(Map<String, CrateMetadata> stats, {this.showProgram}) {
    int compareCrates(String k1, String k2) {
      final compareTally = stats[k1].tally.compareTo(stats[k2].tally);
      if (compareTally != 0) return compareTally;
      return k1.compareTo(k2);
    }

    _stats = SplayTreeMap<String, CrateMetadata>.of(
        stats, (k1, k2) => -compareCrates(k1, k2));
  }

  @override
  Iterable<AnyNode> export() {
    if (_stats.entries.isEmpty) {
      return [Node.plain('Nothing selected')];
    }
    return _stats.entries.map((entry) {
      return Node(
          title: CratesSizeRecord(
              name: AddColor.green(Plain(entry.key)),
              tally: entry.value.tally,
              numPrograms: entry.value.programs.length),
          children: [
            if (showProgram)
              Node(
                  title: StyledString([
                    for (final statistics in [
                      Statistics(entry.value.programs.values)
                    ])
                      Plain('Detected in ${statistics.count} binaries. '
                          'avg: ${formatSize(statistics.mean.round())} '
                          '(${formatSize(statistics.min)} to '
                          '${formatSize(statistics.max)}), '
                          'stdev: ${formatSize(statistics.stdev.round())}.'),
                    if (entry.value.programs.length != null)
                      Plain(' Top appearance:'),
                  ]),
                  children: [
                    for (final program in sortBinaries(entry.value.programs)
                        .take(numProgramsToShow))
                      Node.plain(
                          '${program.key} => ${formatSize(program.value.size)}')
                  ])
          ]);
    });
  }

  /// See `CratesQuery.showProgram`.
  final bool showProgram;

  // TODO(yifeit): Convert this to a configurable value similar to `showProgram`
  static const int numProgramsToShow = 5;

  SplayTreeMap<String, CrateMetadata> _stats;
}
