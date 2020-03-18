// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZXTEST_BASE_ASSERTION_H_
#define ZXTEST_BASE_ASSERTION_H_

#include <zircon/status.h>

#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <zxtest/base/types.h>

namespace zxtest {

// Helper class for handling the error information, plus some logic for printing the correct error
// messages.
class Assertion {
 public:
  Assertion() = delete;
  Assertion(const fbl::String& desc, const fbl::String& expected, const fbl::String& expected_eval,
            const fbl::String& actual, const fbl::String& actual_eval,
            const SourceLocation& location, bool is_fatal);
  Assertion(const fbl::String& desc, const SourceLocation& location, bool is_fatal);
  Assertion(const Assertion&) = delete;
  Assertion(Assertion&&);
  ~Assertion();

  Assertion& operator=(const Assertion&) = delete;
  Assertion& operator=(Assertion&&) = delete;

  // Returns the position at which the assertion happened.
  const SourceLocation& location() const { return location_; }

  // Returns a general description of the asserted condition.
  const fbl::String& description() const { return description_; }

  // Returns the expected value of an equality. For example in ASSERT_EQ(actual, expected) returns
  // the text representation of expected, as it was captured on compile time.
  const fbl::String& expected() const { return expected_; }

  // Returns the expected value of an equality. For example in ASSERT_EQ(actual, expected) returns
  // the text representation of actual, as it was captured on compile time.
  const fbl::String& actual() const { return actual_; }

  // Returns the expected value of an equality. For example in ASSERT_EQ(actual, expected) returns
  // the text representation of expected as it is evaluated at runtime..
  const fbl::String& expected_eval() const { return expected_eval_; }

  // Returns the expected value of an equality. For example in ASSERT_EQ(actual, expected) returns
  // the text representation of actual, as it was captured on runtime.
  const fbl::String& actual_eval() const { return actual_eval_; }

  // Returns true if this assertion is fatal, and test should stop execution. Essentially if the
  // asserting macro is ASSERT_* or EXPECT_*.
  bool is_fatal() const { return is_fatal_; }

  // Returns true if this assertions is value based or manually generated.
  bool has_values() const { return has_values_; }

 private:
  // Text indicating the nature of the assertion. Whether it was expected to be equal, not equal,
  // etc.
  fbl::String description_;
  fbl::String expected_;
  fbl::String expected_eval_;
  fbl::String actual_;
  fbl::String actual_eval_;

  SourceLocation location_;

  bool is_fatal_;
  bool has_values_;
};

// Helper functions used on assertion reporting contexts.
namespace internal {
// Returns a string with the Hex representation of the contents of the buffer pointed by
// ptr. If |ptr| is nullptr, returns "<nullptr>". If |size| is 0 returns <empty>.
fbl::String ToHex(const void* ptr, size_t size);

// It's not necessarily safe to do pointer arithmetic on volatiles because of
// alignment issues, so just print whether the pointer is nullptr/empty/normal.
fbl::String PrintVolatile(volatile const void* ptr, size_t size);
}  // namespace internal

// Specializations exist for primitive types, pointers and |fbl::String|.
template <typename T>
fbl::String PrintValue(const T& value) {
  // TODO(gevalentino): By default generate a hex representation of the memory contents of value.
  return internal::ToHex(&value, sizeof(value));
}

template <typename T>
fbl::String PrintValue(volatile const T& value) {
  return internal::PrintVolatile(&value, sizeof(value));
}

// Template Specialization for integers and char pointers.
template <>
fbl::String PrintValue(const int32_t& value);
template <>
fbl::String PrintValue(const uint32_t& value);
template <>
fbl::String PrintValue(const int64_t& value);
template <>
fbl::String PrintValue(const uint64_t& value);
template <>
fbl::String PrintValue(const fbl::String& value);

// Print a string form of the status, can't be a specializiation of PrintValue
// because zx_status_t is a uint32_t.
fbl::String PrintStatus(zx_status_t status);

// For pointers just print the address.
template <typename T>
fbl::String PrintValue(const T* value) {
  if (value == nullptr) {
    return "<nullptr>";
  }
  return fbl::StringPrintf("%p", static_cast<const void*>(value));
}
template <>
fbl::String PrintValue(const char* value);

// Overloads for string compare.
bool StrCmp(const char* actual, const char* expected);
bool StrCmp(const char* actual, const fbl::String& expected);
bool StrCmp(const fbl::String& actual, const char* expected);
bool StrCmp(const fbl::String& actual, const fbl::String& expected);

// Overloads for string contain.
bool StrContain(const fbl::String& str, const fbl::String& substr);
bool StrContain(const fbl::String& str, const char* substr);
bool StrContain(const char* str, const fbl::String& substr);
bool StrContain(const char* str, const char* substr);

}  // namespace zxtest

#endif  // ZXTEST_BASE_ASSERTION_H_
