// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTING_LEDGER_MATCHER_H_
#define SRC_LEDGER_BIN_TESTING_LEDGER_MATCHER_H_

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fit/result.h>

#include <gmock/gmock.h>

#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/lib/convert/convert.h"

namespace ledger {
namespace internal {
// Adapter from return type of Get/GetInline/Fetch/FetchPartial to a
// fit::result.
class ErrorOrStringResultAdapter {
 public:
  template <typename Result>
  ErrorOrStringResultAdapter(const Result& result);

  const fit::result<std::string, std::pair<zx_status_t, fuchsia::ledger::Error>>& ToResult() const;

 private:
  fit::result<std::string, std::pair<zx_status_t, fuchsia::ledger::Error>> result_;
};
}  // namespace internal

// Matcher that matches a convert::ExtendedStringView against a string-like.
testing::Matcher<convert::ExtendedStringView> MatchesView(testing::Matcher<std::string> matcher);

// Matcher that matches a mem::Buffer against a string.
testing::Matcher<const fuchsia::mem::Buffer&> MatchesBuffer(testing::Matcher<std::string> matcher);

// Matcher that matches a Ledger entry against a pair of matchers on the entry's
// key and value. The entry's priority is not considered in this Matcher.
testing::Matcher<const Entry&> MatchesEntry(
    std::pair<testing::Matcher<std::string>, testing::Matcher<std::string>> matcher);

// Matcher that matches a list of ledger entries against a map from key to
// matchers on the entries' values. The entries' priorities are not considered
// in this Matcher.
testing::Matcher<const std::vector<Entry>&> MatchEntries(
    std::map<std::string, testing::Matcher<std::string>> matchers);

// Matcher that takes the result of Get/GetInline/Fetch/FetchPartial and matches
// its value against a string matcher.
testing::Matcher<internal::ErrorOrStringResultAdapter> MatchesString(
    testing::Matcher<std::string> matcher);

// Matcher that takes the result of Get/GetInline/Fetch/FetchPartial and matches
// its error against an error matcher.
testing::Matcher<internal::ErrorOrStringResultAdapter> MatchesError(
    testing::Matcher<fuchsia::ledger::Error> matcher);

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_TESTING_LEDGER_MATCHER_H_
