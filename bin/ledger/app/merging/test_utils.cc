// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/merging/test_utils.h"

#include "apps/ledger/src/storage/public/constants.h"
#include "gtest/gtest.h"

namespace ledger {
namespace testing {
TestBackoff::TestBackoff(int* get_next_count)
    : get_next_count_(get_next_count) {}
TestBackoff::~TestBackoff() {}

ftl::TimeDelta TestBackoff::GetNext() {
  (*get_next_count_)++;
  return ftl::TimeDelta::FromSeconds(0);
}

void TestBackoff::Reset() {}

std::string MakeObjectId(std::string str) {
  str.resize(storage::kObjectIdSize, '_');
  return str;
}

std::function<void(storage::Journal*)> AddKeyValueToJournal(
    const std::string& key,
    const storage::ObjectId& object_id) {
  return [key, object_id](storage::Journal* journal) {
    EXPECT_EQ(storage::Status::OK, journal->Put(key, MakeObjectId(object_id),
                                                storage::KeyPriority::EAGER));
  };
}

std::function<void(storage::Journal*)> DeleteKeyFromJournal(
    const std::string& key) {
  return [key](storage::Journal* journal) {
    EXPECT_EQ(storage::Status::OK, journal->Delete(key));
  };
}

}  // namespace testing
}  // namespace ledger
