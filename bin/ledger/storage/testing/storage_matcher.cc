// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/testing/storage_matcher.h"

using testing::_;
using testing::AllOf;
using testing::Field;

namespace storage {

testing::Matcher<ObjectIdentifier> DigestMatches(
    testing::Matcher<std::string> matcher) {
  return Field(&ObjectIdentifier::object_digest, matcher);
}

testing::Matcher<Entry> EntryMatches(
    std::pair<testing::Matcher<std::string>, testing::Matcher<ObjectIdentifier>>
        matcher) {
  return EntryMatches({matcher.first, matcher.second, _});
}

testing::Matcher<Entry> EntryMatches(
    std::tuple<testing::Matcher<std::string>,
               testing::Matcher<ObjectIdentifier>,
               testing::Matcher<KeyPriority>>
        matcher) {
  return AllOf(Field(&Entry::key, std::get<0>(matcher)),
               Field(&Entry::object_identifier, std::get<1>(matcher)),
               Field(&Entry::priority, std::get<2>(matcher)));
}

}  // namespace storage
