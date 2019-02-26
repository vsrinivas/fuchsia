// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/ledger_matcher.h"

#include <lib/fsl/vmo/strings.h>

using testing::AllOf;
using testing::Field;
using testing::TypedEq;

namespace ledger {

namespace {
MATCHER_P(InternalViewMatcher, sub_matcher, "") {  // NOLINT
  return ExplainMatchResult(sub_matcher, arg.ToString(), result_listener);
}

MATCHER_P(InternalBufferMatcher, sub_matcher, "") {  // NOLINT
  std::string vmo_content;
  if (!TypedEq<bool>(true).MatchAndExplain(
          fsl::StringFromVmo(arg, &vmo_content), result_listener)) {
    return false;
  }

  return ExplainMatchResult(sub_matcher, vmo_content, result_listener);
}

MATCHER(PointWiseMatchesEntry, "") {  // NOLINT
  auto& a = std::get<0>(arg);
  auto& b = std::get<1>(arg);
  return ExplainMatchResult(MatchesEntry(b), a, result_listener);
}

}  // namespace

testing::Matcher<convert::ExtendedStringView> MatchesView(
    testing::Matcher<std::string> matcher) {
  return InternalViewMatcher(std::move(matcher));
}

testing::Matcher<const fuchsia::mem::Buffer&> MatchesBuffer(
    testing::Matcher<std::string> matcher) {
  return InternalBufferMatcher(std::move(matcher));
}

testing::Matcher<const Entry&> MatchesEntry(
    std::pair<testing::Matcher<std::string>, testing::Matcher<std::string>>
        matcher) {
  return AllOf(Field(&Entry::key, MatchesView(matcher.first)),
               Field(&Entry::value, Pointee(MatchesBuffer(matcher.second))));
}

testing::Matcher<const std::vector<Entry>&> MatchEntries(
    std::map<std::string, testing::Matcher<std::string>> matchers) {
  return Pointwise(PointWiseMatchesEntry(), matchers);
}

}  // namespace ledger
