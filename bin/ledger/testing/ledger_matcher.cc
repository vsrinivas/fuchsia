// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/ledger_matcher.h"

#include <lib/fsl/vmo/strings.h>

using testing::AllOf;
using testing::Field;
using testing::TypedEq;

namespace ledger {

namespace {
MATCHER_P(InternalViewMatcher, sub_matcher, "") {
  return ExplainMatchResult(sub_matcher, arg.ToString(), result_listener);
}

MATCHER_P(InternalBufferMatcher, sub_matcher, "") {
  std::string vmo_content;
  if (!TypedEq<bool>(true).MatchAndExplain(
          fsl::StringFromVmo(arg, &vmo_content), result_listener)) {
    return false;
  }

  return ExplainMatchResult(sub_matcher, vmo_content, result_listener);
}

MATCHER(PointWiseEntryMatches, "") {
  auto& a = std::get<0>(arg);
  auto& b = std::get<1>(arg);
  return ExplainMatchResult(EntryMatches(b), a, result_listener);
}

}  // namespace

testing::Matcher<convert::ExtendedStringView> ViewMatches(
    testing::Matcher<std::string> matcher) {
  return InternalViewMatcher(std::move(matcher));
}

testing::Matcher<const fuchsia::mem::Buffer&> BufferMatches(
    testing::Matcher<std::string> matcher) {
  return InternalBufferMatcher(std::move(matcher));
}

testing::Matcher<const ledger::Entry&> EntryMatches(
    std::pair<testing::Matcher<std::string>, testing::Matcher<std::string>>
        matcher) {
  return AllOf(
      Field(&ledger::Entry::key, ViewMatches(matcher.first)),
      Field(&ledger::Entry::value, Pointee(BufferMatches(matcher.second))));
}

testing::Matcher<const std::vector<ledger::Entry>&> EntriesMatch(
    std::map<std::string, testing::Matcher<std::string>> matchers) {
  return Pointwise(PointWiseEntryMatches(), matchers);
}

}  // namespace ledger
