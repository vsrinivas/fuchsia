// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/device/request_queue.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

namespace {

TEST(RequestQueue, Simple) {
  async::Loop loop{&kAsyncLoopConfigAttachToCurrentThread};

  // Create a queue of size 1.
  RequestQueue queue(loop.dispatcher(), /*max_in_flight=*/1);

  // Enqueue a request, and ensure it runs immediately.
  bool event_ran = false;
  queue.Dispatch([&](RequestQueue::Request request) { event_ran = true; });

  EXPECT_TRUE(event_ran);
}

TEST(RequestQueue, MaxInFlightRespected) {
  async::Loop loop{&kAsyncLoopConfigAttachToCurrentThread};

  // Create a queue of size 1.
  RequestQueue queue(loop.dispatcher(), /*max_in_flight=*/1);

  // Enqueue two requests, but don't mark them as complete.
  constexpr size_t kNumRequests = 2;
  RequestQueue::Request requests[kNumRequests];
  size_t num_requests_run = 0;
  for (size_t i = 0; i < kNumRequests; i++) {
    queue.Dispatch([i, &num_requests_run, &requests](RequestQueue::Request request) {
      requests[i] = std::move(request);
      num_requests_run++;
    });
  }

  // We expect the first request to have run, but nothing else.
  loop.RunUntilIdle();
  EXPECT_EQ(num_requests_run, 1u);

  // Mark it as complete, and ensure the second request has run.
  requests[0].Finish();
  loop.RunUntilIdle();
  EXPECT_EQ(num_requests_run, 2u);

  // Mark the second request as complete.
  requests[1].Finish();
}

}  // namespace
