// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/ledger_matcher.h"

#include <lib/fidl/cpp/optional.h>
#include <lib/fsl/vmo/strings.h>

#include "gtest/gtest.h"

using testing::HasSubstr;
using testing::Not;

namespace ledger {
namespace {

TEST(LedgerMatcher, ExtendedStringViewMatcher) {
  std::string foo = "hello";
  convert::ExtendedStringView view(foo);

  EXPECT_THAT(view, ViewMatches("hello"));
  EXPECT_THAT(view, ViewMatches(HasSubstr("ll")));
  EXPECT_THAT(view, Not(ViewMatches("hello2")));
}

TEST(LedgerMatcher, BufferMatcher) {
  fsl::SizedVmo size_vmo;
  ASSERT_TRUE(fsl::VmoFromString("hello", &size_vmo));
  fuchsia::mem::Buffer buffer = std::move(size_vmo).ToTransport();

  EXPECT_THAT(buffer, BufferMatches("hello"));
  EXPECT_THAT(buffer, BufferMatches(HasSubstr("ll")));
  EXPECT_THAT(buffer, Not(BufferMatches("hello2")));
}

TEST(LedgerMatcher, EntryMatcher) {
  fsl::SizedVmo size_vmo;
  ASSERT_TRUE(fsl::VmoFromString("hello", &size_vmo));
  fuchsia::mem::Buffer buffer = std::move(size_vmo).ToTransport();

  ledger::Entry entry{convert::ToArray("key"),
                      fidl::MakeOptional(std::move(buffer))};

  EXPECT_THAT(entry, EntryMatches({"key", "hello"}));
  EXPECT_THAT(entry, EntryMatches({Not("key2"), HasSubstr("ll")}));
}

TEST(LedgerMatcher, EntriesMatcher) {
  fsl::SizedVmo size_vmo;
  ASSERT_TRUE(fsl::VmoFromString("hello", &size_vmo));
  fuchsia::mem::Buffer buffer = std::move(size_vmo).ToTransport();

  ledger::Entry entry1{convert::ToArray("key1"),
                       fidl::MakeOptional(std::move(buffer))};

  ASSERT_TRUE(fsl::VmoFromString("hello2", &size_vmo));
  buffer = std::move(size_vmo).ToTransport();

  ledger::Entry entry2{convert::ToArray("key2"),
                       fidl::MakeOptional(std::move(buffer))};

  std::vector<ledger::Entry> entries;
  entries.push_back(std::move(entry1));
  entries.push_back(std::move(entry2));

  EXPECT_THAT(entries, EntriesMatch({{"key1", "hello"}, {"key2", "hello2"}}));
  EXPECT_THAT(entries, EntriesMatch({{"key1", HasSubstr("ll")},
                                     {"key2", HasSubstr("ll")}}));
}

}  // namespace
}  // namespace ledger
