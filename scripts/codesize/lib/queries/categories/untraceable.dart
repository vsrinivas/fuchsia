// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

import 'dart:core';

import '../code_category.dart';
import '../index.dart';

class UntraceableContextMixin {
  static final List<RegExp> _regex = [
    // [LOAD #2 [R]]
    // [LOAD #4 [RW]]
    RegExp(r'^\[LOAD #\d+ \[.*\]\]$'),

    // [section .eh_frame]
    // [section .rodata]
    // [section .text]
    RegExp(r'^\[section .*\]$'),
  ];

  bool get isNameUntraceable => _isNameUntraceable(this);

  final _isNameUntraceable = Lazy<bool, CompileUnitContext>(
      (CompileUnitContext self) =>
          matchRegexEnsureAtMostOne(self.name, _regex));
}

class UntraceableCategory extends CodeCategory {
  const UntraceableCategory();

  @override
  String get description => 'Outlined symbols, anonymous globals, '
      'and any other symbols without an indicative name';

  // anon.c02e95ebb747011b7b709c871f48faf0.99.llvm.7252262891747947835
  // .Lanon.93519e6636017643a07f78e678f4f11e.0
  // anon.65e8c07ea73612cb3cb5bebe66e9642a
  static final _anon = RegExp(
      r'^(\.L)?anon\.[a-f0-9][a-f0-9][a-f0-9][a-f0-9][a-f0-9][a-f0-9]+(.*\.llvm\.)?');

  // OUTLINED_FUNCTION_1
  // ** outlined function
  static final _outlined =
      RegExp(r'^(OUTLINED_FUNCTION_\d+|\*\* outlined function)');

  @override
  bool match(
      String symbol, CompileUnitContext compileUnit, ProgramContext program) {
    if (!compileUnit.isNameUntraceable) return false;

    // Symbol name might fallback to compile unit name.
    if (matchRegexEnsureAtMostOne(symbol, UntraceableContextMixin._regex))
      return true;

    if (_anon.hasMatch(symbol)) return true;
    if (_outlined.hasMatch(symbol)) return true;

    return false;
  }
}
