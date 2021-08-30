// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

import 'dart:core';

import '../code_category.dart';
import '../index.dart';

class DiagnosticsContextMixin {
  static final List<RegExp> _regex = [
    // fidling/gen/sdk/fidl/fuchsia.diagnostics[...]/fidl_fuchsia_diagnostics[...].rs
    // fidling/gen/zircon/system/fidl/fuchsia-diagnostics[...]/fidl_fuchsia_diagnostics[...].rs
    // fidling/gen/sdk/fidl/fuchsia.diagnostics[...]/fuchsia.diagnostics[...].fidl.tables.c
    // gen/fuchsia/fidl/fuchsia.diagnostics[...].fidl-tables.c
    // gen/third_party/fuchsia-sdk/sdk/fidl/fuchsia.diagnostics[...]/fidl/fuchsia.diagnostics[...].fidl-tables.c
    RegExp(r'^(fidling/)?gen/?.*/fidl/fuchsia[\._-]diagnostics'),

    // C++ FIDL path patterns:
    RegExp(r'^fidling/gen/fuchsia[\._-]diagnostics.*/fidl/.*\.(cc|c|h|cpp)$'),

    // gen/build/fuchsia/fidl/fuchsia/diagnostics/.../cpp/tables.c
    RegExp(r'fuchsia/fidl/fuchsia/diagnostics.*/cpp'),

    RegExp(
        r'fuchsia/sdk/fidl/fuchsia_diagnostics[a-z0-9_]*/[a-z0-9_]+\.(|cc|c|h|cpp)$'),

    // //src/diagnostics/...
    // //src/lib/diagnostics/...
    // //sdk/lib/inspect/...
    RegExp(r'^\.\./\.\./src/(lib/)?diagnostics/'),
    RegExp(r'^\.\./\.\./sdk/lib/inspect/'),

    // Inspect SDK used from partner builds
    RegExp(
        r'^(\.\./\.\./|third_party/([a-z_-]+/)+)fuchsia[/_-]sdk/([a-z_-]+/)*pkg/inspect/'),

    // [crate: fidl_fuchsia_diagnostics...]
    RegExp(r'^\[crate: fidl_fuchsia_diagnostics(_[a-z][a-z0-9_]+)?\]$'),

    // Crate names extracted by going through the diagnostic GN targets.
    // Defining these in case bloaty was not able to extract the source location
    // from the debug information, in which case it will use the crate fallback:
    RegExp(((String crates) => '^\\[crate: ($crates)\\]\$')([
      'archivist',
      'archivist_benchmarks',
      'archivist_lib',
      'detect',
      'diagnostics_data',
      'diagnostics_hierarchy',
      'diagnostics_reader',
      'diagnostics_stream',
      'diagnostics_testing',
      'diag_tool',
      'encoding_validator',
      'fidl_fuchsia_samplertestcontroller',
      'fidl_fuchsia_validate_logs',
      'fidl_test_inspect_validate',
      'fidl_test_log_stdio',
      'fuchsia_inspect',
      'fuchsia_inspect',
      'fuchsia_inspect_contrib',
      'fuchsia_inspect_derive',
      'injectable_time',
      'inspect_fidl_load',
      'inspect_validator_rust_puppet',
      'iquery',
      'iquery_test_component',
      'launcher',
      'logging_component',
      'log_stats',
      'log_validator_rust_puppet',
      'rust_inspect_benchmarks',
      'sampler',
      'selectors',
      'sink_validator',
      'stdio_puppet',
      'stub_inspect_component',
      'transit_bench',
      'triage',
      'triage_app_lib',
      'v2_argh_wrapper',
      'validating_log_listener',
      'validator',
    ].join('|'))),
  ];

  // Returns if the name of the compile unit belongs to diagnostics code.
  bool get isNameDiagnostics => _isNameDiagnostics(this);

  final _isNameDiagnostics = Lazy<bool, CompileUnitContext>(
      (CompileUnitContext self) =>
          matchRegexEnsureAtMostOne(self.name, _regex));
}

class DiagnosticsCategory extends CodeCategory {
  const DiagnosticsCategory();

  @override
  String get description =>
      'Diagnostics Platform (apps, client libraries, and FIDL generated code)';

  @override
  bool match(
      String symbol, CompileUnitContext compileUnit, ProgramContext program) {
    return compileUnit.isNameDiagnostics;
  }
}
