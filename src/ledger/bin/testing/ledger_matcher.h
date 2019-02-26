// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTING_LEDGER_MATCHER_H_
#define SRC_LEDGER_BIN_TESTING_LEDGER_MATCHER_H_

#include <fuchsia/mem/cpp/fidl.h>
#include <gmock/gmock.h>

#include "peridot/lib/convert/convert.h"
#include "src/ledger/bin/fidl/include/types.h"

namespace ledger {

// Matcher that matches a convert::ExtendedStringView against a string-like.
testing::Matcher<convert::ExtendedStringView> MatchesView(
    testing::Matcher<std::string> matcher);

// Matcher that matches a mem::Buffer against a string.
testing::Matcher<const fuchsia::mem::Buffer&> MatchesBuffer(
    testing::Matcher<std::string> matcher);

// Matcher that matches a Ledger entry against a pair of matchers on the entry's
// key and value. The entry's priority is not considered in this Matcher.
testing::Matcher<const Entry&> MatchesEntry(
    std::pair<testing::Matcher<std::string>, testing::Matcher<std::string>>
        matcher);

// Matcher that matches a list of ledger entries against a map from key to
// matchers on the entries' values. The entries' priorities are not considered
// in this Matcher.
testing::Matcher<const std::vector<Entry>&> MatchEntries(
    std::map<std::string, testing::Matcher<std::string>> matchers);

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_TESTING_LEDGER_MATCHER_H_
