// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <kernel/cmdline.h>
#include <ktl/array.h>
#include <ktl/unique_ptr.h>
#include <lib/unittest/unittest.h>

#include "tests.h"

namespace {

// Cmdline is too big for the stack so use a helper function to heap allocate.
ktl::unique_ptr<Cmdline> MakeCmdline() {
  fbl::AllocChecker ac;
  auto c = ktl::make_unique<Cmdline>(&ac);
  ASSERT(ac.check());
  return c;
}

// Print the command line in hex (for debugging test failures).
void PrintHex(const char* data, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    printf("%02hhx ", data[i]);
  }
}

// "Base case" for EqualsOffset parameter pack template.
//
// See Equals template below.
bool EqualsOffset(const char* data, size_t size, size_t offset, const char* value) {
  if (offset >= size) {
    return false;
  }
  if (strcmp(data + offset, value)) {
    return false;
  }
  return true;
}

// See Equals template below.
template <typename... Rest>
bool EqualsOffset(const char* data, size_t size, size_t offset, const char* value, Rest... rest) {
  if (!EqualsOffset(data, size, offset, value)) {
    return false;
  }
  // Step over the value and the '\0'.
  return EqualsOffset(data, size, offset + strlen(value) + 1, rest...);
}

// Compares |data|, a sequence of \0-terminated strings followed by a final \0, with the |rest| args.
//
// Example:
//   assert(Equals(c, "k1=v1", "k2=v2", "k3=v3"));
template <typename... Rest>
bool Equals(Cmdline* c, Rest... rest) {
  if (!EqualsOffset(c->data(), c->size(), 0, rest...)) {
    printf("Cmdline contains: [ ");
    PrintHex(c->data(), c->size());
    printf("]\n");
    return false;
  }
  return true;
}

bool InitialStateTest() {
  BEGIN_TEST;

  auto c = MakeCmdline();
  ASSERT_EQ(1u, c->size(), "");
  ASSERT_EQ('\0', c->data()[0], "");

  END_TEST;
}

bool AppendBasicTest() {
  BEGIN_TEST;

  // nullptr
  auto c = MakeCmdline();
  c->Append(nullptr);
  EXPECT_TRUE(Equals(c.get(), ""), "");
  EXPECT_EQ(1u, c->size(), "");

  // empty string
  c = MakeCmdline();
  c->Append("");
  EXPECT_TRUE(Equals(c.get(), ""), "");
  EXPECT_EQ(1u, c->size(), "");

  // whitespace
  c = MakeCmdline();
  c->Append("    ");
  EXPECT_TRUE(Equals(c.get(), ""), "");
  EXPECT_EQ(1u, c->size(), "");

  // key only
  c = MakeCmdline();
  c->Append("k");
  ASSERT_TRUE(Equals(c.get(), "k="), "");
  ASSERT_EQ(2 + strlen(c->data()), c->size(), "");

  // key equals
  c = MakeCmdline();
  c->Append("k=");
  ASSERT_TRUE(Equals(c.get(), "k="), "");
  ASSERT_EQ(2 + strlen(c->data()), c->size(), "");

  // two keys
  c = MakeCmdline();
  c->Append("k1 k2");
  ASSERT_TRUE(Equals(c.get(), "k1=", "k2="), "");

  // white space collapsing
  c = MakeCmdline();
  c->Append("  k1    k2   ");
  ASSERT_TRUE(Equals(c.get(), "k1=", "k2="), "");

  // key equals value
  c = MakeCmdline();
  c->Append(" k1=hello  k2=world   ");
  ASSERT_TRUE(Equals(c.get(), "k1=hello", "k2=world"), "");

  // illegal chars become dot
  c = MakeCmdline();
  c->Append(" k1=foo  k2=red" "\xf8" "\x07" "blue");
  ASSERT_TRUE(Equals(c.get(), "k1=foo", "k2=red..blue"), "");

  END_TEST;
}

// Verify that we don't overflow the buffer and that it remains '\0' terminated.
bool OverflowTest() {
  BEGIN_TEST;

  constexpr char kPattern[] = "abcdefg";
  auto c = MakeCmdline();
  for (size_t j = 0; j < Cmdline::kCmdlineMax; ++j) {
    c->Append(kPattern);
  }
  ASSERT_EQ(c->size(), Cmdline::kCmdlineMax, "");
  ASSERT_EQ('\0', c->data()[c->size() - 1], "");
  ASSERT_EQ('\0', c->data()[c->size() - 2], "");
  ASSERT_NE('\0', c->data()[c->size() - 3], "");

  END_TEST;
}

bool GetStringTest() {
  BEGIN_TEST;

  auto c = MakeCmdline();
  ASSERT_EQ(nullptr, c->GetString("k1"), "");
  ASSERT_EQ(nullptr, c->GetString(""), "");
  ASSERT_EQ(c->data(), c->GetString(nullptr), "");

  c->Append("k1=red k2=blue k1=green");
  ASSERT_TRUE(!strcmp(c->GetString("k1"), "red"), "");
  ASSERT_TRUE(!strcmp(c->GetString("k2"), "blue"), "");
  ASSERT_EQ(nullptr, c->GetString(""), "");
  ASSERT_EQ(c->data(), c->GetString(nullptr), "");

  END_TEST;
}

bool GetBoolTest() {
  BEGIN_TEST;

  auto c = MakeCmdline();
  // not found, default is returned
  ASSERT_FALSE(c->GetBool("k0", false), "");
  ASSERT_TRUE(c->GetBool("k0", true), "");

  c->Append("k1=red k2 k3=0 k4=false k5=off k6=01 k7=falseish k8=offset");

  // not found, default is returned
  ASSERT_FALSE(c->GetBool("k0", false), "");
  ASSERT_TRUE(c->GetBool("k0", true), "");

  // values that don't "look like" false are true
  ASSERT_TRUE(c->GetBool("k1", false), "");
  ASSERT_TRUE(c->GetBool("k2", false), "");

  // values that "look like" false are false
  ASSERT_FALSE(c->GetBool("k3", true), "");
  ASSERT_FALSE(c->GetBool("k4", true), "");
  ASSERT_FALSE(c->GetBool("k5", true), "");

  // almost false, but not quite
  ASSERT_TRUE(c->GetBool("k6", false), "");
  ASSERT_TRUE(c->GetBool("k7", false), "");
  ASSERT_TRUE(c->GetBool("k8", false), "");

  END_TEST;
}
bool GetUInt32Test() {
  BEGIN_TEST;

  auto c = MakeCmdline();
  ASSERT_EQ(99u, c->GetUInt32("k1", 99u), "");

  c->Append("k1 k2= k3=42 k4=0 k5=4294967295");
  ASSERT_EQ(99u, c->GetUInt32("k1", 99u), "");
  ASSERT_EQ(99u, c->GetUInt32("k2", 99u), "");
  ASSERT_EQ(42u, c->GetUInt32("k3", 99u), "");
  ASSERT_EQ(0u, c->GetUInt32("k4", 99u), "");
  ASSERT_EQ(UINT32_MAX, c->GetUInt32("k5", 99u), "");
  END_TEST;
}

bool GetUInt64Test() {
  BEGIN_TEST;

  auto c = MakeCmdline();
  ASSERT_EQ(99u, c->GetUInt64("k1", 99u), "");

  c->Append("k1 k2= k3=42 k4=0 k5=9223372036854775807 k6=18446744073709551615");
  ASSERT_EQ(99u, c->GetUInt64("k1", 99u), "");
  ASSERT_EQ(99u, c->GetUInt64("k2", 99u), "");
  ASSERT_EQ(42u, c->GetUInt64("k3", 99u), "");
  ASSERT_EQ(0u, c->GetUInt64("k4", 99u), "");

  // |GetUInt64| is limited to parsing up to INT64_MAX.  Anything higher is saturated to INT64_MAX.
  ASSERT_EQ(static_cast<uint64_t>(INT64_MAX), c->GetUInt64("k5", 99u), "");
  ASSERT_EQ(static_cast<uint64_t>(INT64_MAX), c->GetUInt64("k6", 99u), "");

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(cmdline_tests)
UNITTEST("cmdline_initial_state", InitialStateTest)
UNITTEST("cmdline_append_basic", AppendBasicTest)
UNITTEST("cmdline_overflow", OverflowTest)
UNITTEST("cmdline_get_string", GetStringTest)
UNITTEST("cmdline_get_bool", GetBoolTest)
UNITTEST("cmdline_get_uint32", GetUInt32Test)
UNITTEST("cmdline_get_uint64", GetUInt64Test)
UNITTEST_END_TESTCASE(cmdline_tests, "cmdline_tests", "cmdline_tests");
