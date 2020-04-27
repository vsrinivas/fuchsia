// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_WEAVE_TEST_FIXTURE_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_WEAVE_TEST_FIXTURE_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/gtest/real_loop_fixture.h>

#include <thread>

#include <gtest/gtest.h>

namespace nl::Weave::DeviceLayer::Internal {
namespace testing {

class WeaveTestFixture : public ::gtest::RealLoopFixture {
 public:
  void SetUp() { RealLoopFixture::SetUp(); }

  void TearDown() { RealLoopFixture::TearDown(); }

  void RunFixtureLoop() {
    if (thread_.get_id() != std::thread::id()) {
      return;
    }
    thread_trigger_.store(false);
    thread_ = std::thread([&] { RunLoopUntil([&] { return thread_trigger_.load(); }); });
  }

  void StopFixtureLoop() {
    if (thread_.get_id() == std::thread::id()) {
      return;
    }
    thread_trigger_.store(true);
    thread_.join();
    thread_ = std::thread();
  }

 private:
  std::thread thread_;
  std::atomic_bool thread_trigger_;
};

}  // namespace testing
}  // namespace nl::Weave::DeviceLayer::Internal

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_WEAVE_TEST_FIXTURE_H_
