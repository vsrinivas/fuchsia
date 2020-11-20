// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

import 'dart:io';
import 'dart:typed_data';
import 'package:protobuf/protobuf.dart' as pb;

import 'common_util.dart';
import 'queries/index.dart';
import 'report.pb.dart' as bloaty_report;

/// A list of bloaty reports to analyze.
/// Generated from https://javiercbk.github.io/json_to_dart/.
class AnalysisRequest {
  List<AnalysisItem> items;

  /// The SHA-256 hash of the access heatmap file used to generate the reports.
  /// If a heatmap file was not used, this field may be null.
  String heatmapContentSha;

  AnalysisRequest({this.items, this.heatmapContentSha});

  AnalysisRequest.fromJson(Map<String, dynamic> json) {
    if (json['items'] != null) {
      items = <AnalysisItem>[];
      json['items'].forEach((v) {
        items.add(AnalysisItem.fromJson(v));
      });
    }
    if (json['heatmap_content_sha'] != null) {
      heatmapContentSha = json['heatmap_content_sha'];
    }
  }

  Map<String, dynamic> toJson() {
    final data = <String, dynamic>{};
    if (items != null) {
      data['items'] = items.map((v) => v.toJson()).toList();
    }
    if (heatmapContentSha != null) {
      data['heatmap_content_sha'] = heatmapContentSha;
    }
    return data;
  }
}

/// Generated from https://javiercbk.github.io/json_to_dart/.
class AnalysisItem {
  String path;

  /// If the access heatmap is not used, this field may be null.
  String filteredCounterpart;

  String name;

  AnalysisItem({this.path, this.filteredCounterpart, this.name});

  AnalysisItem.fromJson(Map<String, dynamic> json) {
    path = json['path'];
    filteredCounterpart = json['filtered_counterpart'];
    name = json['name'];
  }

  Map<String, dynamic> toJson() {
    final data = <String, dynamic>{};
    data['path'] = path;
    data['filtered_counterpart'] = filteredCounterpart;
    data['name'] = name;
    return data;
  }

  AnalysisItem absolute(String basePath) {
    String abs(String p) => File(p).isAbsolute ? p : _pathJoin(basePath, p);
    final newPath = abs(path);
    final newFilteredCounterpart = flatMap(filteredCounterpart, abs);
    return AnalysisItem(
        path: newPath, filteredCounterpart: newFilteredCounterpart, name: name);
  }
}

String _pathJoin(String part1, String part2) {
  final buffer = StringBuffer()..write(part1);
  if (!part1.endsWith('/')) buffer.write('/');
  buffer.write(part2);
  return buffer.toString();
}

// Add our custom functionality on top of the protobuf generated types.
// See https://fuchsia.googlesource.com/third_party/bloaty/+/refs/heads/fuchsia/src/report.proto
// for the proto message definitions.

/// A tuple representing the file size and memory size of some entity.
class SizeInfo {
  SizeInfo(this.fileActual, this.vmActual);
  SizeInfo.fromBloaty(bloaty_report.SizeInfo sizeInfo)
      : fileActual = sizeInfo.fileActual.toInt(),
        vmActual = sizeInfo.vmActual.toInt();

  bloaty_report.SizeInfo toBloaty() => bloaty_report.SizeInfo()
    ..fileActual = fileActual
    ..vmActual = vmActual
    ..freeze();

  final int fileActual;
  final int vmActual;

  @override
  String toString() =>
      'SizeInfo { fileActual: $fileActual, vmActual: $vmActual }';
}

/// An ELF symbol, containing name, size, and optional associated rust crate
/// information.
class Symbol {
  Symbol(this.sizes, this.name, this.maybeRustCrate);
  Symbol.fromBloaty(bloaty_report.Symbol symbol)
      : sizes = SizeInfo.fromBloaty(symbol.sizes),
        name = normalizeSymbolNames(symbol.name),
        maybeRustCrate = symbol.maybeRustCrate;

  bloaty_report.Symbol toBloaty() => bloaty_report.Symbol()
    ..sizes = sizes.toBloaty()
    ..name = name
    ..maybeRustCrate = maybeRustCrate
    ..freeze();

  static String normalizeSymbolNames(String name) {
    // Outlined functions have names like OUTLINED_FUNCTION_0, which can
    // appear 1000+ time, and can cause false aliasing. We treat these as
    // special cases by designating them as a placeholder symbols and
    // renaming them to '** outlined function'.
    if (name.startsWith('OUTLINED_FUNCTION_')) {
      return '** outlined function';
    }
    return name;
  }

  /// The file and memory size of this symbol.
  final SizeInfo sizes;

  /// The demangled name of this symbol.
  /// Demangling is currently performed by bloaty.
  final String name;

  /// If not empty, indicates that this symbol was instantiated when compiling
  /// the specified rust crate. Rust performs crate-local monomorphization,
  /// so we might observe for instance an `std::foo` symbol in some user crate.
  /// We would attribute the size to that user crate, since it is the specific
  /// usage from that user crate that caused this symbol to stay in the binary.
  final String maybeRustCrate;

  @override
  String toString() => 'Symbol{ sizes: $sizes, name: $name }';
}

/// A `CompileUnit` is the unit of compilation, typically one or a group of
/// source files, that is part of a bigger crate/library.
///
/// Our most common C++ setup compiles every file individually, so one
/// compile unit correspond to one `.cc` file in a C++ program.
///
/// Our most common Rust setup compiles a group of Rust files together via
/// ThinLTO, so one compile unit roughly correspond to some subset of a
/// Rust crate, bloaty sometimes is able to pin-point the exact
/// `.rs` file that a symbol lives in.
///
/// It is due to these reasons that the `name` in a `CompileUnit` should only
/// be deemed as a best-effort correspondence to actual file names
/// in the source repository.
class CompileUnit {
  CompileUnit(this.sizes, this.symbols, this.name)
      : context = CompileUnitContext(name);
  CompileUnit.fromBloaty(bloaty_report.CompileUnit compileUnit)
      : sizes = SizeInfo.fromBloaty(compileUnit.sizes),
        symbols =
            compileUnit.symbols?.map((s) => Symbol.fromBloaty(s))?.toList(),
        name = compileUnit.name {
    context = CompileUnitContext(name);
  }

  bloaty_report.CompileUnit toBloaty() => bloaty_report.CompileUnit()
    ..sizes = sizes.toBloaty()
    ..symbols.addAll(symbols?.map((e) => e.toBloaty())?.toList())
    ..name = name
    ..freeze();

  /// The file and memory size of this compile unit.
  final SizeInfo sizes;

  /// The list of symbols contained in this compile unit.
  final List<Symbol> symbols;

  /// The name of the compile unit. It may be in one of the following format,
  /// in decreasing order of descriptiveness:
  ///
  /// ### Source files
  /// - foo/bar/baz.c
  /// - foo/bar/baz.cc
  /// - foo/bar/baz.rs
  /// - ...
  ///
  /// ### Rust crates
  /// - [crate: foobar]
  ///
  /// ### Fallback to section names
  /// - [section: .rodata]
  ///
  /// ### Fallback to segment names
  /// - [LOAD #2 [R]]
  /// - [LOAD #4 [RW]]
  final String name;

  /// See `CompileUnitContext`. This object allows queries to attach
  /// domain-specific information to the compile unit as they are run, achieving
  /// information sharing with separation of concerns.
  CompileUnitContext context;
}

/// A hierarchical size breakdown of an ELF binary.
///
/// A report for a binary contains some compile units, which themselves contain
/// some symbols.
class Report {
  Report(this.compileUnits, this.fileTotal, this.vmTotal);

  /// Create a report from its corresponding protobuf representation.
  Report.fromBloaty(String name, bloaty_report.Report report,
      {ProgramContext reuseContext})
      : compileUnits =
            report.compileUnits.map((c) => CompileUnit.fromBloaty(c)).toList(),
        fileTotal = report.fileTotal.toInt(),
        vmTotal = report.vmTotal.toInt() {
    if (reuseContext == null)
      context = ProgramContext(name, this);
    else
      context = reuseContext;
  }

  /// Deserialize a report in protobuf format from `bytes`.
  Report.fromBytes(String name, Uint8List bytes, {ProgramContext reuseContext})
      : this.fromBloaty(
            name,
            bloaty_report.Report.create()
              ..mergeFromCodedBufferReader(
                  pb.CodedBufferReader(bytes, sizeLimit: bytes.length)),
            reuseContext: reuseContext);

  /// Convert the report into its protobuf representation.
  bloaty_report.Report toBloaty() => bloaty_report.Report()
    ..compileUnits.addAll(compileUnits.map((e) => e.toBloaty()).toList())
    ..fileTotal = fileTotal
    ..vmTotal = vmTotal
    ..freeze();

  final List<CompileUnit> compileUnits;

  /// How many bytes is this binary on disk.
  final int fileTotal;

  /// How many bytes does this binary take up when loaded into memory.
  final int vmTotal;

  /// See `ProgramContext`. This object allows queries to attach domain-specific
  /// information to the report as they are run, achieving information sharing
  /// with separation of concerns.
  ProgramContext context;
}
