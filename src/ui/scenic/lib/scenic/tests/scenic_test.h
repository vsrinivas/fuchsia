// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCENIC_TESTS_SCENIC_TEST_H_
#define SRC_UI_SCENIC_LIB_SCENIC_TESTS_SCENIC_TEST_H_

#include <lib/async-testing/test_loop.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/ui/scenic/cpp/session.h>

#include "gtest/gtest.h"
#include "src/ui/scenic/lib/scenic/scenic.h"

namespace scenic_impl {
namespace test {

// Base class that can be specialized to configure a Scenic with the systems
// required for a set of tests.
class ScenicTest : public ::gtest::TestLoopFixture, public EventReporter {
 public:
  ScenicTest() : weak_factory_(this) {}
  ~ScenicTest() override = default;

  Scenic* scenic() { return scenic_.get(); }
  std::unique_ptr<::scenic::Session> CreateSession();

 protected:
  // ::testing::Test virtual method.
  void SetUp() override;

  // ::testing::Test virtual method.
  void TearDown() override;

  // Subclasses may override this to install any systems required by the test;
  // none are installed by default.
  virtual void InitializeScenic(Scenic* scenic);

  // |EventReporter|
  void EnqueueEvent(fuchsia::ui::gfx::Event event) override;
  void EnqueueEvent(fuchsia::ui::input::InputEvent event) override;
  void EnqueueEvent(fuchsia::ui::scenic::Command event) override;
  EventReporterWeakPtr GetWeakPtr() override { return weak_factory_.GetWeakPtr(); }

  std::unique_ptr<sys::ComponentContext> context_;
  std::unique_ptr<Scenic> scenic_;
  std::vector<fuchsia::ui::scenic::Event> events_;

 private:
  fxl::WeakPtrFactory<ScenicTest> weak_factory_;
};

}  // namespace test
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_SCENIC_TESTS_SCENIC_TEST_H_
