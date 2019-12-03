// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/ledger_matcher.h"

#include <zircon/errors.h>

#include "lib/fit/result.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/lib/vmo/strings.h"

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
  if (!TypedEq<bool>(true).MatchAndExplain(ledger::StringFromVmo(arg, &vmo_content),
                                           result_listener)) {
    return false;
  }

  return ExplainMatchResult(sub_matcher, vmo_content, result_listener);
}

MATCHER_P(InternalErrorOrStringResultAdapterErrorMatcher, sub_matcher, "") {
  const fit::result<std::string, std::pair<zx_status_t, fuchsia::ledger::Error>>& result =
      arg.ToResult();
  if (!TypedEq<bool>(true).MatchAndExplain(result.is_error(), result_listener)) {
    return false;
  }
  if (!TypedEq<zx_status_t>(ZX_OK).MatchAndExplain(result.error().first, result_listener)) {
    return false;
  }
  return ExplainMatchResult(sub_matcher, result.error().second, result_listener);
}

MATCHER_P(InternalErrorOrStringResultAdapterStringMatcher, sub_matcher, "") {
  const fit::result<std::string, std::pair<zx_status_t, fuchsia::ledger::Error>>& result =
      arg.ToResult();
  if (!TypedEq<bool>(true).MatchAndExplain(result.is_ok(), result_listener)) {
    return false;
  }
  return ExplainMatchResult(sub_matcher, result.value(), result_listener);
}

MATCHER(PointWiseMatchesEntry, "") {  // NOLINT
  auto& a = std::get<0>(arg);
  auto& b = std::get<1>(arg);
  return ExplainMatchResult(MatchesEntry(b), a, result_listener);
}

}  // namespace

namespace internal {

// Generic implementation for Get/Fetch/FetchPartial.
template <typename Result>
ErrorOrStringResultAdapter::ErrorOrStringResultAdapter(const Result& result) {
  if (result.is_err()) {
    result_ = fit::error(std::make_pair(ZX_OK, result.err()));
    return;
  }
  std::string value;
  bool status = ledger::StringFromVmo(result.response().buffer, &value);
  if (!status) {
    result_ = fit::error(std::make_pair(ZX_ERR_BAD_HANDLE, fuchsia::ledger::Error::NETWORK_ERROR));
    return;
  }
  result_ = fit::ok(std::move(value));
}

// Specialize for GetInline that directly has a vector.
template <>
ErrorOrStringResultAdapter::ErrorOrStringResultAdapter(
    const fuchsia::ledger::PageSnapshot_GetInline_Result& result) {
  if (result.is_err()) {
    result_ = fit::error(std::make_pair(ZX_OK, result.err()));
    return;
  }
  result_ = fit::ok(convert::ToString(result.response().value.value));
}

// Instantiate for all possible type, as the template implementation is in the
// .cc file.
template ErrorOrStringResultAdapter::ErrorOrStringResultAdapter(
    const fuchsia::ledger::PageSnapshot_Get_Result&);
template ErrorOrStringResultAdapter::ErrorOrStringResultAdapter(
    const fuchsia::ledger::PageSnapshot_Fetch_Result&);
template ErrorOrStringResultAdapter::ErrorOrStringResultAdapter(
    const fuchsia::ledger::PageSnapshot_FetchPartial_Result&);

const fit::result<std::string, std::pair<zx_status_t, fuchsia::ledger::Error>>&
ErrorOrStringResultAdapter::ToResult() const {
  return result_;
}

}  // namespace internal

testing::Matcher<convert::ExtendedStringView> MatchesView(testing::Matcher<std::string> matcher) {
  return InternalViewMatcher(std::move(matcher));
}

testing::Matcher<const fuchsia::mem::Buffer&> MatchesBuffer(testing::Matcher<std::string> matcher) {
  return InternalBufferMatcher(std::move(matcher));
}

testing::Matcher<const Entry&> MatchesEntry(
    std::pair<testing::Matcher<std::string>, testing::Matcher<std::string>> matcher) {
  return AllOf(Field("key", &Entry::key, MatchesView(matcher.first)),
               Field("value", &Entry::value, Pointee(MatchesBuffer(matcher.second))));
}

testing::Matcher<const std::vector<Entry>&> MatchEntries(
    std::map<std::string, testing::Matcher<std::string>> matchers) {
  return Pointwise(PointWiseMatchesEntry(), matchers);
}

testing::Matcher<internal::ErrorOrStringResultAdapter> MatchesString(
    testing::Matcher<std::string> matcher) {
  return InternalErrorOrStringResultAdapterStringMatcher(std::move(matcher));
}

testing::Matcher<internal::ErrorOrStringResultAdapter> MatchesError(
    testing::Matcher<fuchsia::ledger::Error> matcher) {
  return InternalErrorOrStringResultAdapterErrorMatcher(std::move(matcher));
}

}  // namespace ledger
