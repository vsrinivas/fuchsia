// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_WEAVE_TEST_FIXTURE_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_WEAVE_TEST_FIXTURE_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/gtest/real_loop_fixture.h>

#include <memory>
#include <thread>

#include <gtest/gtest.h>

namespace nl::Weave::DeviceLayer::Internal {
namespace testing {
namespace internal {
// An empty resource to satisfy template requirements of WeaveTestFixture in the
// event that no resource is used.
class EmptyResource {};
}

// A RealLoopFixture that runs the loop in a separate thread, allowing blocking
// synchronous calls to be made in the test code.
//
// Optionally supports resources which must outlive the fixture loop. The
// resource will be constructed before the RealLoopFixture is set up and
// destroyed after the RealLoopFixture is torn down.
//
// Resource must be default-constructible. If the subclass needs to connntrol
// construction or use a non-default-constructible resource, then a resource of
// unique_ptr<ActualResource> may be used instead.
template <class Resource = internal::EmptyResource>
class WeaveTestFixture : public ::gtest::RealLoopFixture {
 public:
  void SetUp() override {
    resource_ = std::make_unique<Resource>();
    RealLoopFixture::SetUp();
  }

  void TearDown() override {
    RealLoopFixture::TearDown();
    resource_.reset();
  }

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

 protected:
  Resource& resource() {
    return *resource_;
  }

 private:
  std::thread thread_;
  std::atomic_bool thread_trigger_;
  std::unique_ptr<Resource> resource_;
};

}  // namespace testing
}  // namespace nl::Weave::DeviceLayer::Internal

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_WEAVE_TEST_FIXTURE_H_
