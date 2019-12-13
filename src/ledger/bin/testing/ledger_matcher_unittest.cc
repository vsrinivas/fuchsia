// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/ledger_matcher.h"

#include <lib/fidl/cpp/optional.h>

#include "gtest/gtest.h"
#include "src/ledger/lib/vmo/strings.h"

using testing::HasSubstr;
using testing::Not;

namespace ledger {
namespace {

TEST(LedgerMatcher, ExtendedStringViewMatcher) {
  std::string foo = "hello";
  convert::ExtendedStringView view(foo);

  EXPECT_THAT(view, MatchesView("hello"));
  EXPECT_THAT(view, MatchesView(HasSubstr("ll")));
  EXPECT_THAT(view, Not(MatchesView("hello2")));
}

TEST(LedgerMatcher, BufferMatcher) {
  SizedVmo size_vmo;
  ASSERT_TRUE(VmoFromString("hello", &size_vmo));
  fuchsia::mem::Buffer buffer = std::move(size_vmo).ToTransport();

  EXPECT_THAT(buffer, MatchesBuffer("hello"));
  EXPECT_THAT(buffer, MatchesBuffer(HasSubstr("ll")));
  EXPECT_THAT(buffer, Not(MatchesBuffer("hello2")));
}

TEST(LedgerMatcher, EntryMatcher) {
  SizedVmo size_vmo;
  ASSERT_TRUE(VmoFromString("hello", &size_vmo));
  fuchsia::mem::Buffer buffer = std::move(size_vmo).ToTransport();

  Entry entry{convert::ToArray("key"), fidl::MakeOptional(std::move(buffer))};

  EXPECT_THAT(entry, MatchesEntry({"key", "hello"}));
  EXPECT_THAT(entry, MatchesEntry({Not("key2"), HasSubstr("ll")}));
}

TEST(LedgerMatcher, EntriesMatcher) {
  SizedVmo size_vmo;
  ASSERT_TRUE(VmoFromString("hello", &size_vmo));
  fuchsia::mem::Buffer buffer = std::move(size_vmo).ToTransport();

  Entry entry1{convert::ToArray("key1"), fidl::MakeOptional(std::move(buffer))};

  ASSERT_TRUE(VmoFromString("hello2", &size_vmo));
  buffer = std::move(size_vmo).ToTransport();

  Entry entry2{convert::ToArray("key2"), fidl::MakeOptional(std::move(buffer))};

  std::vector<Entry> entries;
  entries.push_back(std::move(entry1));
  entries.push_back(std::move(entry2));

  EXPECT_THAT(entries, MatchEntries({{"key1", "hello"}, {"key2", "hello2"}}));
  EXPECT_THAT(entries, MatchEntries({{"key1", HasSubstr("ll")}, {"key2", HasSubstr("ll")}}));
}

}  // namespace
}  // namespace ledger
