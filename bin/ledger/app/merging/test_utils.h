// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_MERGING_TEST_UTILS_H_
#define APPS_LEDGER_SRC_APP_MERGING_TEST_UTILS_H_

#include <functional>
#include <memory>

#include "apps/ledger/src/backoff/backoff.h"
#include "apps/ledger/src/storage/public/journal.h"
#include "apps/ledger/src/storage/public/types.h"

namespace ledger {
namespace testing {
// Dummy implementation of a backoff policy, which always returns zero backoff
// time..
class TestBackoff : public backoff::Backoff {
 public:
  TestBackoff(int* get_next_count);
  ~TestBackoff() override;

  ftl::TimeDelta GetNext() override;

  void Reset() override;

  int* get_next_count_;
};

// Resize id to the required size, adding trailing underscores if needed.
std::string MakeObjectId(std::string str);

// Returns a function that, when executed, adds the provided key and object to a
// journal.
std::function<void(storage::Journal*)> AddKeyValueToJournal(
    const std::string& key,
    const storage::ObjectId& object_id);

std::function<void(storage::Journal*)> DeleteKeyFromJournal(
    const std::string& key);

}  // namespace testing
}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_MERGING_TEST_UTILS_H_
