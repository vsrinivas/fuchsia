// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <thread>

#include "gtest/gtest.h"
#include "platform_event.h"

class TestEvent {
 public:
  static void Test() {
    auto event = std::shared_ptr<magma::PlatformEvent>(magma::PlatformEvent::Create());

    std::vector<std::thread> threads;

    for (uint32_t iter = 0; iter < 100; iter++) {
      threads.emplace_back([](std::shared_ptr<magma::PlatformEvent> event) { event->Wait(); },
                           event);
    }

    std::this_thread::yield();

    event->Signal();

    for (uint32_t iter = 0; iter < 100; iter++) {
      threads[iter].join();
    }
  }
};

TEST(PlatformEvent, Test) { TestEvent::Test(); }
