// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/tests/mocks.h"
#include "garnet/lib/ui/scenic/tests/dummy_system.h"
#include "garnet/lib/ui/scenic/tests/scenic_gfx_test.h"
#include "garnet/lib/ui/scenic/tests/scenic_test.h"
#include "gtest/gtest.h"

namespace {
class Dependency : public scenic_impl::System {
 public:
  using System::System;
  scenic_impl::CommandDispatcherUniquePtr CreateCommandDispatcher(
      scenic_impl::CommandDispatcherContext context) override {
    ++num_dispatchers_;
    // We don't actually expect anyone to call this, but for logging purposes, we will record it.
    return nullptr;
  };

  uint32_t GetNumDispatchers() { return num_dispatchers_; }

 private:
  uint32_t num_dispatchers_ = 0;
};

}  // namespace

namespace scenic_impl {
namespace test {

TEST_F(ScenicTest, SessionCreatedAfterAllSystemsInitialized) {
  auto mock_system = scenic()->RegisterSystem<DummySystem>(false);

  EXPECT_EQ(scenic()->num_sessions(), 0U);

  // Request session creation, which doesn't occur yet because system isn't
  // initialized.
  auto session = CreateSession();
  EXPECT_EQ(scenic()->num_sessions(), 0U);

  // Initializing the system allows the session to be created.
  mock_system->SetToInitialized();
  EXPECT_EQ(scenic()->num_sessions(), 1U);
}

TEST_F(ScenicTest, DependenciesBlockSessionCreation) {
  auto mock_system = scenic()->RegisterSystem<DummySystem>(false);
  EXPECT_EQ(scenic()->num_sessions(), 0U);
  auto dependency = std::make_unique<Dependency>(
      SystemContext(scenic_->app_context(), inspect_deprecated::Node(), /*quit_callback*/ nullptr),
      false);
  scenic()->RegisterDependency(dependency.get());

  auto session = CreateSession();
  EXPECT_EQ(scenic()->num_sessions(), 0U);

  // This should not create the session, as the dependency is still uninitialized.
  mock_system->SetToInitialized();
  EXPECT_EQ(scenic()->num_sessions(), 0U);
  EXPECT_EQ(mock_system->GetNumDispatchers(), 0U);
  EXPECT_EQ(dependency->GetNumDispatchers(), 0U);

  // At this point, all systems are initialized, but we don't dispatch to dependencies.
  dependency->SetToInitialized();
  EXPECT_EQ(scenic()->num_sessions(), 1U);
  EXPECT_EQ(mock_system->GetNumDispatchers(), 1U);
  EXPECT_EQ(dependency->GetNumDispatchers(), 0U);
}

TEST_F(ScenicTest, DependenciesBlockSessionCreationReverseOrder) {
  auto mock_system = scenic()->RegisterSystem<DummySystem>(false);
  EXPECT_EQ(scenic()->num_sessions(), 0U);
  auto dependency = std::make_unique<Dependency>(
      SystemContext(scenic_->app_context(), inspect_deprecated::Node(), /*quit_callback*/ nullptr),
      false);
  scenic()->RegisterDependency(dependency.get());

  auto session = CreateSession();
  EXPECT_EQ(scenic()->num_sessions(), 0U);

  // This test is identical to DependenciesBlockSessionCreation, but it initializes the dependency
  // first.
  dependency->SetToInitialized();
  EXPECT_EQ(scenic()->num_sessions(), 0U);
  EXPECT_EQ(mock_system->GetNumDispatchers(), 0U);
  EXPECT_EQ(dependency->GetNumDispatchers(), 0U);

  mock_system->SetToInitialized();
  EXPECT_EQ(scenic()->num_sessions(), 1U);
  EXPECT_EQ(mock_system->GetNumDispatchers(), 1U);
  EXPECT_EQ(dependency->GetNumDispatchers(), 0U);
}

TEST_F(ScenicGfxTest, InvalidPresentCall_ShouldDestroySession) {
  EXPECT_EQ(scenic()->num_sessions(), 0U);
  auto session = CreateSession();
  EXPECT_EQ(scenic()->num_sessions(), 1U);

  session->Present(/*Presentation Time*/ 10,
                   /*Present Callback*/ [](auto) {});

  // Trigger error by making a present call with an earlier presentation time
  // than the previous call to present
  session->Present(/*Presentation Time*/ 0,
                   /*Present Callback*/ [](auto) {});

  RunLoopUntilIdle();

  EXPECT_EQ(scenic()->num_sessions(), 0U);
}

}  // namespace test
}  // namespace scenic_impl
