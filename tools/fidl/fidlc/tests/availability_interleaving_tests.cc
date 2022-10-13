// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/include/fidl/diagnostics.h"
#include "tools/fidl/fidlc/tests/error_test.h"
#include "tools/fidl/fidlc/tests/test_library.h"

// This file tests ways of interleaving the availability of a source element
// with that of a target element that it references. See also
// versioning_tests.cc and decomposition_tests.cc.

namespace {

struct TestCase {
  // A code describing how to order the availabilities relative to each other,
  // using (a, d, r, l) for the source and (A, D, R, L) for the target:
  //
  //     source: @available(added=a, deprecated=d, removed=r/l, legacy=...)
  //     target: @available(added=A, deprecated=D, removed=R/L, legacy=...)
  //
  // For example, "AadrR" means: add target, add source, deprecate source,
  // remove source, remove target. Additionally, the character "=" is used to
  // align two values. For example, "a=A" means the source and target are added
  // at the same version, and never deprecated/removed.
  //
  // Using l/L instead of r/R means the element is removed with legacy=true.
  //
  // Must contain at least "a" and "A", but all others are optional.
  std::string_view code;

  // Expected errors. The order does not matter, and the list does not need to
  // be complete, because this file contains a large number of test cases and
  // stricter requirements would make it painful to update when errors change.
  std::vector<const fidl::DiagnosticDef*> errors = {};

  struct Attributes {
    std::string source_available;
    std::string target_available;
  };

  // Generates the @available attributes for source and target.
  Attributes Format() const {
    std::stringstream source, target;
    source << "@available(";
    target << "@available(";
    int version = 1;
    for (auto c : code) {
      switch (c) {
        case 'a':
          source << "added=" << version;
          break;
        case 'd':
          source << ", deprecated=" << version;
          break;
        case 'r':
          source << ", removed=" << version;
          break;
        case 'l':
          source << ", removed=" << version << ", legacy = true";
          break;
        case 'A':
          target << "added=" << version;
          break;
        case 'D':
          target << ", deprecated=" << version;
          break;
        case 'R':
          target << ", removed=" << version;
          break;
        case 'L':
          target << ", removed=" << version << ", legacy=true";
          break;
        case '=':
          version -= 2;
          break;
      }
      ++version;
    }
    source << ")";
    target << ")";
    return Attributes{source.str(), target.str()};
  }

  // Compiles the library and asserts that it conforms to the test case.
  void CompileAndAssert(TestLibrary& library) const {
    if (errors.empty()) {
      ASSERT_COMPILED(library);
      return;
    }
    ASSERT_FALSE(library.Compile());
    std::set<std::string_view> actual_errors;
    for (auto& actual_error : library.errors()) {
      actual_errors.insert(actual_error->def.msg);
    }
    for (auto expected_error : errors) {
      EXPECT_TRUE(actual_errors.count(expected_error->msg), "missing error '%.*s'",
                  static_cast<int>(expected_error->msg.size()), expected_error->msg.data());
    }
  }
};

// These cases (except for some extras at the bottom) were generated with the
// following Python code:
//
//     def go(x, y):
//         if x is None or y is None:
//             return set()
//         if not (x or y):
//             return {""}
//         rest = lambda x: x[1:] if x else None
//         rx, ry, rxy = go(rest(x), y), go(x, rest(y)), go(rest(x), rest(y))
//         return {*rx, *ry, *rxy, *(x[0] + s for s in rx), *(y[0] + s for s in ry),
//                 *(f"{x[0]}={y[0]}{s}" for s in rxy)}
//
//     print("\n".join(sorted(s for s in go("adr", "ADR") if "a" in s and "A" in s)))
//
const TestCase kTestCases[] = {
    {"ADRa", {&fidl::ErrNameNotFound}},
    {"ADRad", {&fidl::ErrNameNotFound}},
    {"ADRadr", {&fidl::ErrNameNotFound}},
    {"ADRar", {&fidl::ErrNameNotFound}},
    {"ADa", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"ADa=R", {&fidl::ErrNameNotFound}},
    {"ADa=Rd", {&fidl::ErrNameNotFound}},
    {"ADa=Rdr", {&fidl::ErrNameNotFound}},
    {"ADa=Rr", {&fidl::ErrNameNotFound}},
    {"ADaR", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"ADaRd", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"ADaRdr", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"ADaRr", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"ADad", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"ADad=R", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"ADad=Rr", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"ADadR", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"ADadRr", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"ADadr", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"ADadr=R", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"ADadrR", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"ADar", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"ADar=R", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"ADarR", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"ARa", {&fidl::ErrNameNotFound}},
    {"ARad", {&fidl::ErrNameNotFound}},
    {"ARadr", {&fidl::ErrNameNotFound}},
    {"ARar", {&fidl::ErrNameNotFound}},
    {"Aa"},
    {"Aa=D", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"Aa=DR", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"Aa=DRd", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"Aa=DRdr", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"Aa=DRr", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"Aa=Dd", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"Aa=Dd=R", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"Aa=Dd=Rr", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"Aa=DdR", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"Aa=DdRr", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"Aa=Ddr", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"Aa=Ddr=R", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"Aa=DdrR", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"Aa=Dr", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"Aa=Dr=R", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"Aa=DrR", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"Aa=R", {&fidl::ErrNameNotFound}},
    {"Aa=Rd", {&fidl::ErrNameNotFound}},
    {"Aa=Rdr", {&fidl::ErrNameNotFound}},
    {"Aa=Rr", {&fidl::ErrNameNotFound}},
    {"AaD", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"AaDR", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"AaDRd", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"AaDRdr", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"AaDRr", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"AaDd", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"AaDd=R", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"AaDd=Rr", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"AaDdR", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"AaDdRr", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"AaDdr", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"AaDdr=R", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"AaDdrR", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"AaDr", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"AaDr=R", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"AaDrR", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"AaR", {&fidl::ErrNameNotFound}},
    {"AaRd", {&fidl::ErrNameNotFound}},
    {"AaRdr", {&fidl::ErrNameNotFound}},
    {"AaRr", {&fidl::ErrNameNotFound}},
    {"Aad"},
    {"Aad=D"},
    {"Aad=DR", {&fidl::ErrNameNotFound}},
    {"Aad=DRr", {&fidl::ErrNameNotFound}},
    {"Aad=Dr"},
    {"Aad=Dr=R"},
    {"Aad=DrR"},
    {"Aad=R", {&fidl::ErrNameNotFound}},
    {"Aad=Rr", {&fidl::ErrNameNotFound}},
    {"AadD"},
    {"AadDR", {&fidl::ErrNameNotFound}},
    {"AadDRr", {&fidl::ErrNameNotFound}},
    {"AadDr"},
    {"AadDr=R"},
    {"AadDrR"},
    {"AadR", {&fidl::ErrNameNotFound}},
    {"AadRr", {&fidl::ErrNameNotFound}},
    {"Aadr"},
    {"Aadr=D"},
    {"Aadr=DR"},
    {"Aadr=R"},
    {"AadrD"},
    {"AadrDR"},
    {"AadrR"},
    {"Aar"},
    {"Aar=D"},
    {"Aar=DR"},
    {"Aar=R"},
    {"AarD"},
    {"AarDR"},
    {"AarR"},
    {"a=A"},
    {"a=AD", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"a=ADR", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"a=ADRd", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"a=ADRdr", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"a=ADRr", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"a=ADd", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"a=ADd=R", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"a=ADd=Rr", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"a=ADdR", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"a=ADdRr", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"a=ADdr", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"a=ADdr=R", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"a=ADdrR", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"a=ADr", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"a=ADr=R", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"a=ADrR", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"a=AR", {&fidl::ErrNameNotFound}},
    {"a=ARd", {&fidl::ErrNameNotFound}},
    {"a=ARdr", {&fidl::ErrNameNotFound}},
    {"a=ARr", {&fidl::ErrNameNotFound}},
    {"a=Ad"},
    {"a=Ad=D"},
    {"a=Ad=DR", {&fidl::ErrNameNotFound}},
    {"a=Ad=DRr", {&fidl::ErrNameNotFound}},
    {"a=Ad=Dr"},
    {"a=Ad=Dr=R"},
    {"a=Ad=DrR"},
    {"a=Ad=R", {&fidl::ErrNameNotFound}},
    {"a=Ad=Rr", {&fidl::ErrNameNotFound}},
    {"a=AdD"},
    {"a=AdDR", {&fidl::ErrNameNotFound}},
    {"a=AdDRr", {&fidl::ErrNameNotFound}},
    {"a=AdDr"},
    {"a=AdDr=R"},
    {"a=AdDrR"},
    {"a=AdR", {&fidl::ErrNameNotFound}},
    {"a=AdRr", {&fidl::ErrNameNotFound}},
    {"a=Adr"},
    {"a=Adr=D"},
    {"a=Adr=DR"},
    {"a=Adr=R"},
    {"a=AdrD"},
    {"a=AdrDR"},
    {"a=AdrR"},
    {"a=Ar"},
    {"a=Ar=D"},
    {"a=Ar=DR"},
    {"a=Ar=R"},
    {"a=ArD"},
    {"a=ArDR"},
    {"a=ArR"},
    {"aA", {&fidl::ErrNameNotFound}},
    {"aAD", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"aADR", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"aADRd", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"aADRdr", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"aADRr", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"aADd", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"aADd=R", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"aADd=Rr", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"aADdR", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"aADdRr", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"aADdr", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"aADdr=R", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"aADdrR", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"aADr", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"aADr=R", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"aADrR", {&fidl::ErrInvalidReferenceToDeprecated, &fidl::ErrNameNotFound}},
    {"aAR", {&fidl::ErrNameNotFound}},
    {"aARd", {&fidl::ErrNameNotFound}},
    {"aARdr", {&fidl::ErrNameNotFound}},
    {"aARr", {&fidl::ErrNameNotFound}},
    {"aAd", {&fidl::ErrNameNotFound}},
    {"aAd=D", {&fidl::ErrNameNotFound}},
    {"aAd=DR", {&fidl::ErrNameNotFound}},
    {"aAd=DRr", {&fidl::ErrNameNotFound}},
    {"aAd=Dr", {&fidl::ErrNameNotFound}},
    {"aAd=Dr=R", {&fidl::ErrNameNotFound}},
    {"aAd=DrR", {&fidl::ErrNameNotFound}},
    {"aAd=R", {&fidl::ErrNameNotFound}},
    {"aAd=Rr", {&fidl::ErrNameNotFound}},
    {"aAdD", {&fidl::ErrNameNotFound}},
    {"aAdDR", {&fidl::ErrNameNotFound}},
    {"aAdDRr", {&fidl::ErrNameNotFound}},
    {"aAdDr", {&fidl::ErrNameNotFound}},
    {"aAdDr=R", {&fidl::ErrNameNotFound}},
    {"aAdDrR", {&fidl::ErrNameNotFound}},
    {"aAdR", {&fidl::ErrNameNotFound}},
    {"aAdRr", {&fidl::ErrNameNotFound}},
    {"aAdr", {&fidl::ErrNameNotFound}},
    {"aAdr=D", {&fidl::ErrNameNotFound}},
    {"aAdr=DR", {&fidl::ErrNameNotFound}},
    {"aAdr=R", {&fidl::ErrNameNotFound}},
    {"aAdrD", {&fidl::ErrNameNotFound}},
    {"aAdrDR", {&fidl::ErrNameNotFound}},
    {"aAdrR", {&fidl::ErrNameNotFound}},
    {"aAr", {&fidl::ErrNameNotFound}},
    {"aAr=D", {&fidl::ErrNameNotFound}},
    {"aAr=DR", {&fidl::ErrNameNotFound}},
    {"aAr=R", {&fidl::ErrNameNotFound}},
    {"aArD", {&fidl::ErrNameNotFound}},
    {"aArDR", {&fidl::ErrNameNotFound}},
    {"aArR", {&fidl::ErrNameNotFound}},
    {"ad=A", {&fidl::ErrNameNotFound}},
    {"ad=AD", {&fidl::ErrNameNotFound}},
    {"ad=ADR", {&fidl::ErrNameNotFound}},
    {"ad=ADRr", {&fidl::ErrNameNotFound}},
    {"ad=ADr", {&fidl::ErrNameNotFound}},
    {"ad=ADr=R", {&fidl::ErrNameNotFound}},
    {"ad=ADrR", {&fidl::ErrNameNotFound}},
    {"ad=AR", {&fidl::ErrNameNotFound}},
    {"ad=ARr", {&fidl::ErrNameNotFound}},
    {"ad=Ar", {&fidl::ErrNameNotFound}},
    {"ad=Ar=D", {&fidl::ErrNameNotFound}},
    {"ad=Ar=DR", {&fidl::ErrNameNotFound}},
    {"ad=Ar=R", {&fidl::ErrNameNotFound}},
    {"ad=ArD", {&fidl::ErrNameNotFound}},
    {"ad=ArDR", {&fidl::ErrNameNotFound}},
    {"ad=ArR", {&fidl::ErrNameNotFound}},
    {"adA", {&fidl::ErrNameNotFound}},
    {"adAD", {&fidl::ErrNameNotFound}},
    {"adADR", {&fidl::ErrNameNotFound}},
    {"adADRr", {&fidl::ErrNameNotFound}},
    {"adADr", {&fidl::ErrNameNotFound}},
    {"adADr=R", {&fidl::ErrNameNotFound}},
    {"adADrR", {&fidl::ErrNameNotFound}},
    {"adAR", {&fidl::ErrNameNotFound}},
    {"adARr", {&fidl::ErrNameNotFound}},
    {"adAr", {&fidl::ErrNameNotFound}},
    {"adAr=D", {&fidl::ErrNameNotFound}},
    {"adAr=DR", {&fidl::ErrNameNotFound}},
    {"adAr=R", {&fidl::ErrNameNotFound}},
    {"adArD", {&fidl::ErrNameNotFound}},
    {"adArDR", {&fidl::ErrNameNotFound}},
    {"adArR", {&fidl::ErrNameNotFound}},
    {"adr=A", {&fidl::ErrNameNotFound}},
    {"adr=AD", {&fidl::ErrNameNotFound}},
    {"adr=ADR", {&fidl::ErrNameNotFound}},
    {"adr=AR", {&fidl::ErrNameNotFound}},
    {"adrA", {&fidl::ErrNameNotFound}},
    {"adrAD", {&fidl::ErrNameNotFound}},
    {"adrADR", {&fidl::ErrNameNotFound}},
    {"adrAR", {&fidl::ErrNameNotFound}},
    {"ar=A", {&fidl::ErrNameNotFound}},
    {"ar=AD", {&fidl::ErrNameNotFound}},
    {"ar=ADR", {&fidl::ErrNameNotFound}},
    {"ar=AR", {&fidl::ErrNameNotFound}},
    {"arA", {&fidl::ErrNameNotFound}},
    {"arAD", {&fidl::ErrNameNotFound}},
    {"arADR", {&fidl::ErrNameNotFound}},
    {"arAR", {&fidl::ErrNameNotFound}},

    // Some manual cases for LEGACY. Doing all permutations would grow the list
    // above from 252 to 730 entries.
    {"AadDlL"},
    {"AadlD"},
    {"AalD", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"AalDL", {&fidl::ErrInvalidReferenceToDeprecated}},
    {"AalDR", {&fidl::ErrNameNotFound}},
    {"AalL"},
    {"a=AL", {&fidl::ErrNameNotFound}},
    {"a=Ad=Dl=L"},
    {"a=Al"},
    {"a=Al=L"},
    {"a=Al=R", {&fidl::ErrNameNotFound}},
    {"a=Ar=L"},
    {"alAL", {&fidl::ErrNameNotFound}},
};

// Substitutes replacement for placeholder in str.
void substitute(std::string& str, std::string_view placeholder, std::string_view replacement) {
  str.replace(str.find(placeholder), placeholder.size(), replacement);
}

TEST(AvailabilityInterleavingTests, SameLibrary) {
  for (auto& test_case : kTestCases) {
    auto attributes = test_case.Format();
    std::string fidl = R"FIDL(
@available(added=1)
library example;

${source_available}
const SOURCE bool = TARGET;

${target_available}
const TARGET bool = false;
)FIDL";
    substitute(fidl, "${source_available}", attributes.source_available);
    substitute(fidl, "${target_available}", attributes.target_available);
    TestLibrary library(fidl);
    ASSERT_NO_FAILURES(test_case.CompileAndAssert(library), "code: %.*s, fidl:\n\n%s",
                       static_cast<int>(test_case.code.size()), test_case.code.data(),
                       fidl.c_str());
  }
}

// Tests compilation of example_fidl and dependency_fidl after substituting
// ${source_available} in example_fidl and ${target_available} in
// dependency_fidl using the values from test_case.
void TestExternalLibrary(const TestCase& test_case, std::string example_fidl,
                         std::string dependency_fidl) {
  SharedAmongstLibraries shared;
  auto attributes = test_case.Format();
  substitute(dependency_fidl, "${target_available}", attributes.target_available);
  TestLibrary dependency(&shared, "dependency.fidl", dependency_fidl);
  ASSERT_COMPILED(dependency);
  substitute(example_fidl, "${source_available}", attributes.source_available);
  TestLibrary example(&shared, "example.fidl", example_fidl);
  ASSERT_NO_FAILURES(test_case.CompileAndAssert(example),
                     "code: %.*s, dependency.fidl:\n\n%s\n\nexample.fidl:\n\n%s",
                     static_cast<int>(test_case.code.size()), test_case.code.data(),
                     dependency_fidl.c_str(), example_fidl.c_str());
}

TEST(AvailabilityInterleavingTests, DeclToDeclExternal) {
  std::string example_fidl = R"FIDL(
@available(added=1)
library platform.example;

using platform.dependency;

${source_available}
const SOURCE bool = platform.dependency.TARGET;
)FIDL";
  std::string dependency_fidl = R"FIDL(
@available(added=1)
library platform.dependency;

${target_available}
const TARGET bool = false;
)FIDL";
  for (auto& test_case : kTestCases) {
    ASSERT_NO_FAILURES(TestExternalLibrary(test_case, example_fidl, dependency_fidl));
  }
}

TEST(AvailabilityInterleavingTests, LibraryToLibraryExternal) {
  std::string example_fidl = R"FIDL(
${source_available}
library platform.example;

using platform.dependency;

const SOURCE bool = platform.dependency.TARGET;
)FIDL";
  std::string dependency_fidl = R"FIDL(
${target_available}
library platform.dependency;

const TARGET bool = false;
)FIDL";
  for (auto& test_case : kTestCases) {
    ASSERT_NO_FAILURES(TestExternalLibrary(test_case, example_fidl, dependency_fidl));
  }
}

TEST(AvailabilityInterleavingTests, LibraryToDeclExternal) {
  std::string example_fidl = R"FIDL(
${source_available}
library platform.example;

using platform.dependency;

const SOURCE bool = platform.dependency.TARGET;
)FIDL";
  std::string dependency_fidl = R"FIDL(
@available(added=1)
library platform.dependency;

${target_available}
const TARGET bool = false;
)FIDL";
  for (auto& test_case : kTestCases) {
    ASSERT_NO_FAILURES(TestExternalLibrary(test_case, example_fidl, dependency_fidl));
  }
}

TEST(AvailabilityInterleavingTests, DeclToLibraryExternal) {
  std::string example_fidl = R"FIDL(
@available(added=1)
library platform.example;

using platform.dependency;

${source_available}
const SOURCE bool = platform.dependency.TARGET;
)FIDL";
  std::string dependency_fidl = R"FIDL(
${target_available}
library platform.dependency;

const TARGET bool = false;
)FIDL";
  for (auto& test_case : kTestCases) {
    ASSERT_NO_FAILURES(TestExternalLibrary(test_case, example_fidl, dependency_fidl));
  }
}

}  // namespace
