// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/tests/mocks.h"
#include "garnet/lib/ui/scenic/tests/dummy_system.h"
#include "garnet/lib/ui/scenic/tests/scenic_gfx_test.h"
#include "garnet/lib/ui/scenic/tests/scenic_test.h"
#include "gtest/gtest.h"

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
