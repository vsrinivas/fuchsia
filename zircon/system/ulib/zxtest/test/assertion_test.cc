// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE filename.

#include <zircon/assert.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include <zxtest/base/assertion.h>
#include <zxtest/base/types.h>

#include "test-registry.h"

namespace zxtest {

namespace test {

constexpr char kDescription[] = "desc";
constexpr char kExpectedSymbol[] = "ESymbol";
constexpr char kExpectedValue[] = "EValue";
constexpr char kActualSymbol[] = "ASymbol";
constexpr char kActualValue[] = "AValue";
constexpr char kFile[] = "File";
std::string kMessage = "message";
constexpr int kLine = 120;
constexpr bool kIsFatal = false;
constexpr SourceLocation kLocation = {.filename = kFile, .line_number = kLine};
zxtest::Message trace_message = zxtest::Message(kMessage, kLocation);

void AssertionHasValues() {
  std::vector<zxtest::Message*> empty_traces{&trace_message};
  Assertion assertion(kDescription, kExpectedSymbol, kExpectedValue, kActualSymbol, kActualValue,
                      kLocation, kIsFatal, empty_traces);

  ZX_ASSERT_MSG(assertion.description() == kDescription, "Assertion::description() is incorrect.");

  ZX_ASSERT_MSG(assertion.expected() == kExpectedSymbol, "Assertion::expected() is incorrect.");

  ZX_ASSERT_MSG(assertion.expected_eval() == kExpectedValue,
                "Assertion::expected_eval() is incorrect.");

  ZX_ASSERT_MSG(assertion.actual() == kActualSymbol, "Assertion::actual() is incorrect.");

  ZX_ASSERT_MSG(assertion.actual_eval() == kActualValue, "Assertion::actual_eval() is incorrect.");

  ZX_ASSERT_MSG(assertion.location().filename == kFile, "Assertion::location() is incorrect.");
  ZX_ASSERT_MSG(assertion.location().line_number == kLine, "Assertion::location() is incorrect.");

  ZX_ASSERT_MSG(assertion.is_fatal() == kIsFatal, "Assertion::fatal() is incorrect.");

  ZX_ASSERT_MSG(assertion.has_values(), "Assertion should have values.");

  ZX_ASSERT_MSG(!assertion.scoped_traces().empty(),
                "Assertion::scoped_traces() should not be empty.");

  ZX_ASSERT_MSG(assertion.scoped_traces()[0]->location().filename == kFile,
                "Assertion::scoped_traces() has SourceLocation with incorrect path.");

  ZX_ASSERT_MSG(assertion.scoped_traces()[0]->location().line_number == kLine,
                "Assertion::scoped_traces() has SourceLocation with incorrect line number.");

  ZX_ASSERT_MSG(assertion.scoped_traces()[0]->text() == kMessage,
                "Assertion::scoped_traces() has incorrect message.");
}

void AssertionHasNoValues() {
  std::vector<zxtest::Message*> empty_traces{};
  Assertion assertion(kDescription, {.filename = kFile, .line_number = kLine}, kIsFatal,
                      empty_traces);

  ZX_ASSERT_MSG(assertion.description() == kDescription, "Assertion::description() is incorrect.");

  ZX_ASSERT_MSG(assertion.expected().empty(), "Assertion::expected() is incorrect.");

  ZX_ASSERT_MSG(assertion.expected_eval().empty(), "Assertion::expected_eval() is incorrect.");

  ZX_ASSERT_MSG(assertion.actual().empty(), "Assertion::actual() is incorrect.");

  ZX_ASSERT_MSG(assertion.actual_eval().empty(), "Assertion::actual_eval() is incorrect.");

  ZX_ASSERT_MSG(assertion.location().filename == kFile, "Assertion::location() is incorrect.");
  ZX_ASSERT_MSG(assertion.location().line_number == kLine, "Assertion::location() is incorrect.");

  ZX_ASSERT_MSG(assertion.is_fatal() == kIsFatal, "Assertion::fatal() is incorrect.");

  ZX_ASSERT_MSG(!assertion.has_values(), "Assertion should not have values.");

  ZX_ASSERT_MSG(assertion.scoped_traces().empty(), "Assertion::scoped_traces() should be empty.");
}

}  // namespace test
}  // namespace zxtest
