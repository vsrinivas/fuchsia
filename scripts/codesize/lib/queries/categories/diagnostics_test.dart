// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';

import '../index.dart';

void main() {
  group('isNameDiagnostics', () {
    void pos(String name) =>
        expect(CompileUnitContext(name).isNameDiagnostics, equals(true),
            reason: '`$name` should match one of the compile unit name regexes '
                'for diagnostics');
    test('Positive cases (C++ Diagnostics FIDL)', () {
      pos(r'fidling/gen/sdk/fidl/fuchsia.diagnostics/fuchsia.diagnostics.fidl.tables.c');
      pos(r'fidling/gen/zircon/system/fidl/fuchsia-diagnostics/fuchsia-diagnostics.fidl.tables.c');
      pos(r'gen/build/fuchsia/fidl/fuchsia/diagnostics/cpp/tables.c');
      pos(r'gen/third_party/fuchsia-sdk/sdk/fidl/fuchsia.diagnostics/fidl/fuchsia.diagnostics.fidl-tables.c');
      pos(r'gen/system/fidl/fuchsia-diagnostics/fuchsia-diagnostics.tables/tables.c');
      pos(r'gen/fuchsia/fidl/fuchsia.diagnostics.fidl-tables.c');
      pos(r'gen/foo/bar/fuchsia/fidl/fuchsia.diagnostics/fidl/fuchsia.diagnostics.fidl-tables.c');
      pos(r'bla/bla/bla/fuchsia/sdk/fidl/fuchsia_diagnostics/fuchsia_diagnostics_tables.c');
    });

    test('Positive cases (Rust Diagnostics FIDL)', () {
      pos(r'fidling/gen/sdk/fidl/fuchsia.diagnostics/fuchsia.diagnostics/fidl_fuchsia_diagnostics.rs');
      pos(r'fidling/gen/zircon/system/fidl/fuchsia-diagnostics/fidl_fuchsia_diagnostics.rs');
    });

    test('Positive cases (Rust Crates)', () {
      pos(r'[crate: archivist]');
      pos(r'[crate: detect]');
    });
  });
}
