// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/event_sender.h>

#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <zxtest/zxtest.h>

#include "lib/fidl/cpp/interface_ptr.h"
#include "lib/fidl/cpp/test/async_loop_for_test.h"
#include "lib/fidl/cpp/test/frobinator_impl.h"

namespace fidl {
namespace {

TEST(EventSender, Events) {
  fidl::test::AsyncLoopForTest loop;

  fidl::test::frobinator::FrobinatorPtr ptr;
  auto request = ptr.NewRequest();

  std::vector<std::string> hrobs;
  ptr.events().Hrob = [&hrobs](StringPtr value) {
    EXPECT_TRUE(value.has_value());
    hrobs.push_back(value.value());
  };

  auto background = std::thread([&request]() {
    // Notice that this thread does not have an async loop.
    EventSender<fidl::test::frobinator::Frobinator> sender(std::move(request));
    EXPECT_TRUE(sender.channel().is_valid());
    sender.events().Hrob("one");
  });
  background.join();

  loop.RunUntilIdle();

  EXPECT_EQ(1u, hrobs.size());
  EXPECT_EQ("one", hrobs[0]);
}

}  // namespace
}  // namespace fidl
