// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <memory>

#include <zxtest/zxtest.h>

#include "lib/cmdline.h"

namespace {

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

// Compares |data|, a sequence of \0-terminated strings followed by a final \0, with the |rest|
// args.
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

TEST(KernelCmdlineTest, InitialState) {
  auto c = std::make_unique<Cmdline>();
  ASSERT_EQ(1u, c->size());
  ASSERT_EQ('\0', c->data()[0]);
}

TEST(KernelCmdLineTest, AppendBasic) {
  // nullptr
  auto c = std::make_unique<Cmdline>();
  c->Append(nullptr);
  EXPECT_TRUE(Equals(c.get(), ""));
  EXPECT_EQ(1u, c->size());

  // empty string
  c = std::make_unique<Cmdline>();
  c->Append("");
  EXPECT_TRUE(Equals(c.get(), ""));
  EXPECT_EQ(1u, c->size());

  // single whitespace
  c = std::make_unique<Cmdline>();
  c->Append(" ");
  EXPECT_TRUE(Equals(c.get(), ""));
  EXPECT_EQ(1u, c->size());

  // multiple whitespace
  c = std::make_unique<Cmdline>();
  c->Append("    ");
  EXPECT_TRUE(Equals(c.get(), ""));
  EXPECT_EQ(1u, c->size());

  // key only
  c = std::make_unique<Cmdline>();
  c->Append("k");
  EXPECT_TRUE(Equals(c.get(), "k="));
  EXPECT_EQ(2 + strlen(c->data()), c->size());

  // whitespace before key
  c = std::make_unique<Cmdline>();
  c->Append(" k");
  EXPECT_TRUE(Equals(c.get(), "k="));
  EXPECT_EQ(2 + strlen(c->data()), c->size());

  // key equals
  c = std::make_unique<Cmdline>();
  c->Append("k=");
  EXPECT_TRUE(Equals(c.get(), "k="));
  EXPECT_EQ(2 + strlen(c->data()), c->size());

  // two keys
  c = std::make_unique<Cmdline>();
  c->Append("k1 k2");
  EXPECT_TRUE(Equals(c.get(), "k1=", "k2="));

  // white space collapsing
  c = std::make_unique<Cmdline>();
  c->Append("  k1    k2   ");
  EXPECT_TRUE(Equals(c.get(), "k1=", "k2="));

  // key equals value
  c = std::make_unique<Cmdline>();
  c->Append(" k1=hello  k2=world   ");
  EXPECT_TRUE(Equals(c.get(), "k1=hello", "k2=world"));

  // illegal chars become dot
  c = std::make_unique<Cmdline>();
  c->Append(
      " k1=foo  k2=red"
      "\xf8"
      "\x07"
      "blue");
  EXPECT_TRUE(Equals(c.get(), "k1=foo", "k2=red..blue"));
}

// Verify that we don't overflow the buffer and that it remains '\0' terminated.
TEST(KernelCmdLineTest, Overflow) {
  constexpr char kPattern[] = "abcdefg";
  auto c = std::make_unique<Cmdline>();
  for (size_t j = 0; j < Cmdline::kCmdlineMax; ++j) {
    c->Append(kPattern);
  }
  ASSERT_EQ(c->size(), Cmdline::kCmdlineMax);
  EXPECT_EQ('\0', c->data()[c->size() - 1]);
  EXPECT_EQ('\0', c->data()[c->size() - 2]);
  EXPECT_NE('\0', c->data()[c->size() - 3]);
}

TEST(KernelCmdLineTest, GetString) {
  auto c = std::make_unique<Cmdline>();
  EXPECT_EQ(nullptr, c->GetString("k1"));
  EXPECT_EQ(nullptr, c->GetString(""));
  EXPECT_EQ(c->data(), c->GetString(nullptr));

  c->Append("k1=red k2=blue k1=green");
  EXPECT_TRUE(!strcmp(c->GetString("k1"), "red"));
  EXPECT_TRUE(!strcmp(c->GetString("k2"), "blue"));
  EXPECT_EQ(nullptr, c->GetString(""));
  EXPECT_EQ(c->data(), c->GetString(nullptr));
}

TEST(KernelCmdLineTest, GetBool) {
  auto c = std::make_unique<Cmdline>();
  // not found, default is returned
  EXPECT_FALSE(c->GetBool("k0", false));
  EXPECT_TRUE(c->GetBool("k0", true));

  c->Append("k1=red k2 k3=0 k4=false k5=off k6=01 k7=falseish k8=offset");

  // not found, default is returned
  EXPECT_FALSE(c->GetBool("k0", false));
  EXPECT_TRUE(c->GetBool("k0", true));

  // values that don't "look like" false are true
  EXPECT_TRUE(c->GetBool("k1", false));
  EXPECT_TRUE(c->GetBool("k2", false));

  // values that "look like" false are false
  EXPECT_FALSE(c->GetBool("k3", true));
  EXPECT_FALSE(c->GetBool("k4", true));
  EXPECT_FALSE(c->GetBool("k5", true));

  // almost false, but not quite
  EXPECT_TRUE(c->GetBool("k6", false));
  EXPECT_TRUE(c->GetBool("k7", false));
  EXPECT_TRUE(c->GetBool("k8", false));
}

TEST(KernelCmdLineTest, GetUInt32) {
  auto c = std::make_unique<Cmdline>();
  EXPECT_EQ(99u, c->GetUInt32("k1", 99u));

  c->Append("k1 k2= k3=42 k4=0 k5=4294967295");
  EXPECT_EQ(99u, c->GetUInt32("k1", 99u));
  EXPECT_EQ(99u, c->GetUInt32("k2", 99u));
  EXPECT_EQ(42u, c->GetUInt32("k3", 99u));
  EXPECT_EQ(0u, c->GetUInt32("k4", 99u));
  EXPECT_EQ(UINT32_MAX, c->GetUInt32("k5", 99u));
}

TEST(KernelCmdLineTest, GetUInt64) {
  auto c = std::make_unique<Cmdline>();
  EXPECT_EQ(99u, c->GetUInt64("k1", 99u));

  c->Append("k1 k2= k3=42 k4=0 k5=9223372036854775807 k6=18446744073709551615");
  EXPECT_EQ(99u, c->GetUInt64("k1", 99u));
  EXPECT_EQ(99u, c->GetUInt64("k2", 99u));
  EXPECT_EQ(42u, c->GetUInt64("k3", 99u));
  EXPECT_EQ(0u, c->GetUInt64("k4", 99u));

  // |GetUInt64| is limited to parsing up to INT64_MAX.  Anything higher is saturated to INT64_MAX.
  EXPECT_EQ(static_cast<uint64_t>(INT64_MAX), c->GetUInt64("k5", 99u));
  EXPECT_EQ(static_cast<uint64_t>(INT64_MAX), c->GetUInt64("k6", 99u));
}

}  // namespace
