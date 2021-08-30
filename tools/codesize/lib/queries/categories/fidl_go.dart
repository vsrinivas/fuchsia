// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

import 'dart:core';

import '../code_category.dart';
import '../index.dart';
import '../source_lang.dart';

class GoFidlContextMixin {
  /// Regex matching compile units (roughly go packages) for fuchsia FIDL
  /// libraries.
  static final List<RegExp> _regex = [
    // fidl/fuchsia/io
    // fidl/fuchsia/storage/metrics
    RegExp(r'^fidl/fuchsia/([a-z0-9]+/)*[a-z0-9]+$'),
  ];

  bool get isNameGoFidl => _isNameGoFidl(this);

  final _isNameGoFidl =
      Lazy<bool, CompileUnitContext>((CompileUnitContext self) {
    if (self.isCFamilyFileExtension || self.isRust) return false;
    if (matchRegexEnsureAtMostOne(self.name, _regex)) return true;
    return false;
  });
}

class GoFidlCategory extends CodeCategory implements SomeFidlCategory {
  const GoFidlCategory();

  @override
  String get description =>
      'Go FIDL bindings (both the runtime and generated code)';

  @override
  bool match(
      String symbol, CompileUnitContext compileUnit, ProgramContext program) {
    // This assumes that Go programs are 100% homogenously written in Go, to
    // the extent that the `SourceLang` classification is always accruate.
    // This is true for the products we care about as of July 2020.
    if (program.lang != SourceLang.go) return false;
    if (compileUnit.isNameGoFidl) return true;

    return false;
  }
}
