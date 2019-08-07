// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/cloud_provider_firestore/bin/firestore/firestore_service_impl.h"

#include <lib/gtest/test_loop_fixture.h>

#include <queue>
#include <string>

#include <google/protobuf/util/time_util.h>
#include <gtest/gtest.h>

namespace cloud_provider_firestore {

namespace {

class PollEventsTest : public ::gtest::TestLoopFixture {
 public:
  PollEventsTest() = default;
  ~PollEventsTest() override = default;
};

TEST_F(PollEventsTest, PollSingleEvent) {
  bool tag_called = false;
  fit::function<void(bool)> on_complete = [&tag_called](bool ok) { tag_called = true; };

  std::queue<void*> tags_to_return;
  tags_to_return.push(&on_complete);

  std::function<bool(void** tag, bool* ok)> get_next_tag = [tags = move(tags_to_return)](
                                                               void** tag, bool* ok) mutable {
    if (tags.empty()) {
      return false;
    }
    *tag = tags.front();
    tags.pop();
    *ok = true;
    return true;
  };

  PollEvents(get_next_tag, dispatcher());
  EXPECT_TRUE(RunLoopUntilIdle());
  EXPECT_TRUE(tag_called);
}

// Verifies that we correctly handle a case where GetNext returns true, but
// doesn't actually set the tag pointer to a new address. This is a regression
// test for http://fxb/LE-721, where we were unintentionally calling a stale
// memory address in such a scenario.
TEST_F(PollEventsTest, HandleGetNextNotSettingTheFunctionPointer) {
  int tag_call_count = 0;
  fit::function<void(bool)> on_complete = [&tag_call_count](bool ok) { tag_call_count++; };

  std::queue<void*> tags_to_return;
  tags_to_return.push(&on_complete);
  bool return_true_once_more = true;
  std::function<bool(void** tag, bool* ok)> get_next_tag =
      [tags = move(tags_to_return), &return_true_once_more](void** tag, bool* ok) mutable {
        if (tags.empty()) {
          if (return_true_once_more) {
            // Return true w/o setting |tag| or |ok| to a new value.
            return_true_once_more = false;
            return true;
          }
          return false;
        }
        *tag = tags.front();
        tags.pop();
        *ok = true;
        return true;
      };

  PollEvents(get_next_tag, dispatcher());
  EXPECT_TRUE(RunLoopUntilIdle());
  EXPECT_EQ(tag_call_count, 1);
}

}  // namespace

}  // namespace cloud_provider_firestore
