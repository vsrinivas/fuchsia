// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "gtest/gtest.h"
#include "lib/async/cpp/future.h"
#include "peridot/bin/user_runner/story_runner/story_storage.h"
#include "peridot/lib/ledger_client/page_id.h"
#include "peridot/lib/testing/test_with_ledger.h"

namespace modular {
namespace {

class StoryStorageTest : public testing::TestWithLedger {
 protected:
  std::unique_ptr<StoryStorage> CreateStorage(std::string page_id) {
    return std::make_unique<StoryStorage>(ledger_client(), MakePageId(page_id));
  }
};

TEST_F(StoryStorageTest, Na) {}

}  // namespace
}  // namespace modular
