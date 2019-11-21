// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/testing/storage_matcher.h"

#include <set>

#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/convert/convert.h"

using testing::_;
using testing::AllOf;
using testing::Eq;
using testing::Field;
using testing::ResultOf;
using testing::UnorderedElementsAreArray;

namespace storage {

testing::Matcher<const ObjectIdentifier&> MatchesDigest(
    testing::Matcher<const std::string&> matcher) {
  return Property("object_digest", &ObjectIdentifier::object_digest,
                  Property("Serialize", &ObjectDigest::Serialize, matcher));
}

testing::Matcher<const ObjectIdentifier&> MatchesDigest(
    testing::Matcher<const ObjectDigest&> matcher) {
  return Property("object_digest", &ObjectIdentifier::object_digest, matcher);
}

testing::Matcher<const Entry&> MatchesEntry(
    std::pair<testing::Matcher<const std::string&>, testing::Matcher<const ObjectIdentifier&>>
        matcher) {
  return MatchesEntry({matcher.first, matcher.second, _});
}

testing::Matcher<const Entry&> MatchesEntry(
    std::tuple<testing::Matcher<const std::string&>, testing::Matcher<const ObjectIdentifier&>,
               testing::Matcher<const KeyPriority&>>
        matcher) {
  return AllOf(Field("key", &Entry::key, std::get<0>(matcher)),
               Field("object_identifier", &Entry::object_identifier, std::get<1>(matcher)),
               Field("priority", &Entry::priority, std::get<2>(matcher)));
}

testing::Matcher<const Commit&> MatchesCommit(const CommitId& id,
                                              const std::set<CommitId>& parent_ids) {
  return AllOf(ResultOf([](const Commit& commit) { return commit.GetId(); }, Eq(id)),
               ResultOf(
                   [](const Commit& commit) {
                     std::set<CommitId> parent_ids;
                     for (const CommitIdView& parent_id : commit.GetParentIds()) {
                       parent_ids.insert(convert::ToString(parent_id));
                     }
                     return parent_ids;
                   },
                   UnorderedElementsAreArray(parent_ids)));
}

testing::Matcher<const PageStorage::CommitIdAndBytes&> MatchesCommitIdAndBytes(
    testing::Matcher<std::string> id, testing::Matcher<std::string> bytes) {
  return AllOf(Field("id", &PageStorage::CommitIdAndBytes::id, id),
               Field("bytes", &PageStorage::CommitIdAndBytes::bytes, bytes));
}

}  // namespace storage
