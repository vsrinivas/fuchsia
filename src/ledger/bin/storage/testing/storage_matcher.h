// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_TESTING_STORAGE_MATCHER_H_
#define SRC_LEDGER_BIN_STORAGE_TESTING_STORAGE_MATCHER_H_

#include <set>
#include <tuple>

#include <gmock/gmock.h>

#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/bin/storage/public/types.h"

namespace storage {

// Matcher that matches an ObjectIdentifier against a matcher for its digest.
// Its key_index and deletion_scoped_id are ignored.
testing::Matcher<const ObjectIdentifier&> MatchesDigest(
    testing::Matcher<const std::string&> matcher);
testing::Matcher<const ObjectIdentifier&> MatchesDigest(
    testing::Matcher<const ObjectDigest&> matcher);

// Matcher that matches a Ledger entry against a pair of matchers on the entry's
// key and object_identifier. The entry's priority is not considered in this
// Matcher.
testing::Matcher<const Entry&> MatchesEntry(
    std::pair<testing::Matcher<const std::string&>, testing::Matcher<const ObjectIdentifier&>>
        matcher);

// Matcher that matches a Ledger entry against a tuple of matchers on the
// entry's key, object_identifier and priority.
testing::Matcher<const Entry&> MatchesEntry(
    std::tuple<testing::Matcher<const std::string&>, testing::Matcher<const ObjectIdentifier&>,
               testing::Matcher<const KeyPriority&>>
        matcher);

// Matcher that matches a Commit against its expected ID and parent IDs.
testing::Matcher<const Commit&> MatchesCommit(const CommitId& id,
                                              const std::set<CommitId>& parent_ids);

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_TESTING_STORAGE_MATCHER_H_
