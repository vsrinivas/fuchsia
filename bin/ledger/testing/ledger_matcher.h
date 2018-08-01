// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTING_LEDGER_MATCHER_H_
#define PERIDOT_BIN_LEDGER_TESTING_LEDGER_MATCHER_H_

#include <fuchsia/mem/cpp/fidl.h>
#include <gmock/gmock.h>

#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/lib/convert/convert.h"

namespace ledger {

// Matcher that matches a convert::ExtendedStringView against a string-like.
testing::Matcher<convert::ExtendedStringView> ViewMatches(
    testing::Matcher<std::string> matcher);

// Matcher that matches a mem::Buffer against a string.
testing::Matcher<const fuchsia::mem::Buffer&> BufferMatches(
    testing::Matcher<std::string> matcher);

// Matcher that matches a Ledger entry against a pair of matchers on the entry's
// key and value. The entry's priority is not considered in this Matcher.
testing::Matcher<const ledger::Entry&> EntryMatches(
    std::pair<testing::Matcher<std::string>, testing::Matcher<std::string>>
        matcher);

// Matcher that matches a list of ledger entries against a map from key to
// matchers on the entries' values. The entries' priorities are not considered
// in this Matcher.
testing::Matcher<const std::vector<ledger::Entry>&> EntriesMatch(
    std::map<std::string, testing::Matcher<std::string>> matchers);

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_TESTING_LEDGER_MATCHER_H_
