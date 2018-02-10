// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/mozart/tests/dummy_system.h"
#include "garnet/lib/ui/mozart/tests/mozart_test.h"

namespace mz {
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

TEST_F(MozartTest, SessionCreatedAfterAllSystemsInitialized) {
  auto mock_system =
      mozart()->RegisterSystem<test::MockSystemWithDelayedInitialization>();

  EXPECT_EQ(0U, mozart()->num_sessions());

  // Request session creation, which doesn't occur yet because system isn't
  // initialized.
  ui_mozart::SessionPtr session;
  mozart()->CreateSession(session.NewRequest(), nullptr);
  EXPECT_EQ(0U, mozart()->num_sessions());

  // Initializing the system allows the session to be created.
  mock_system->SetToInitialized();
  EXPECT_EQ(1U, mozart()->num_sessions());
}

}  // namespace test
}  // namespace mz
