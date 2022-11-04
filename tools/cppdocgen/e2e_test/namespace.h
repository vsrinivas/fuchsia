// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_CPPDOCGEN_E2E_TEST_NAMESPACE_H_
#define TOOLS_CPPDOCGEN_E2E_TEST_NAMESPACE_H_

/// # Namespace testing
///
/// This tests that the basic stuff works inside a namespace. The main thing is that the output be
/// properly qualified and that links to other items are correct.

namespace myns {

// Here is a link to [myns::EnumInsideNamespace].
struct StructInsideNamespace {
  int a;
};

// Here is a link to [myns::StructInsideNamespace].
enum EnumInsideNamespace {
  kValue1,
  kValue2,
};

typedef StructInsideNamespace StructInsideNamespaceTypedef;
using StructInsideNamespaceUsing = StructInsideNamespace;

// Here is a link to [myns::ClassInsideNamespace]. In particular, see
// [myns::ClassInsideNamespace::SomeFunction].
int FunctionInsideNamespace();

// Here is a link to [myns::FunctionInsideNamespace].
class ClassInsideNamespace {
 public:
  ClassInsideNamespace();

  int SomeFunction();
};

}  // namespace myns

#endif  // TOOLS_CPPDOCGEN_E2E_TEST_NAMESPACE_H_
