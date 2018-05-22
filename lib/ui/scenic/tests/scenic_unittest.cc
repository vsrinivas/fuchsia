// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/tests/dummy_system.h"
#include "garnet/lib/ui/scenic/tests/scenic_test.h"

namespace scenic {
namespace test {

namespace {

class MockSystemWithDelayedInitialization : public DummySystem {
 public:
  // Expose to tests.
  using System::SetToInitialized;

  explicit MockSystemWithDelayedInitialization(SystemContext context)
      : DummySystem(std::move(context), false) {}
};

}  // anonymous namespace

TEST_F(ScenicTest, SessionCreatedAfterAllSystemsInitialized) {
  auto mock_system =
      scenic()->RegisterSystem<test::MockSystemWithDelayedInitialization>();

  EXPECT_EQ(0U, scenic()->num_sessions());

  // Request session creation, which doesn't occur yet because system isn't
  // initialized.
  fuchsia::ui::scenic::SessionPtr session;
  scenic()->CreateSession(session.NewRequest(), nullptr);
  EXPECT_EQ(0U, scenic()->num_sessions());

  // Initializing the system allows the session to be created.
  mock_system->SetToInitialized();
  EXPECT_EQ(1U, scenic()->num_sessions());
}

}  // namespace test
}  // namespace scenic
