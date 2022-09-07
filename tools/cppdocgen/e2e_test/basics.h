// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_CPPDOCGEN_E2E_TEST_BASICS_H_
#define TOOLS_CPPDOCGEN_E2E_TEST_BASICS_H_

/// # Custom file header
///
/// This is the docstring for this file. It should appear at the top of the generated documentation
///
/// ## Just the basics
///
/// This test file contains the basics of the document generator.

// Documentation for the API flag.
#define API_FLAG_1 1
#define API_FLAG_2 2

// This is a structure that defines some values. The values appear inside the structure.
//
// The first value has no docstring, the second one does.
struct SimpleTestStructure {
  int a;

  char b;  // Some end-of-line documentation.

  // Some documentation for the `b` member of the `SimpleTestStructure`.
  double c;
};

enum class MyEnum : char {
  kValue1 = 1,
  kValue2 = 2,
};

// This is an extern global value.
extern int kGlobalValue;

typedef SimpleTestStructure SimpleTestStructureTypedef;
using SImpleTestStructureUsing = SimpleTestStructure;

#endif  // TOOLS_CPPDOCGEN_E2E_TEST_BASICS_H_
