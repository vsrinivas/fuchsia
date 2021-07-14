// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/lib/injector/injector.h"

#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/input/lib/injector/tests/mocks/mock_injector_registry.h"

namespace input::test {

class InjectorTest : public gtest::TestLoopFixture {
 protected:
  sys::testing::ComponentContextProvider context_provider_;
};

TEST_F(InjectorTest, Multiple_MarkSceneReady_ShouldNotCauseMultipleRegistrations) {
  MockInjectorRegistry injector_registry(context_provider_);

  const auto [context_control_ref, context_view_ref] = scenic::ViewRefPair::New();
  const auto [target_control_ref, target_view_ref] = scenic::ViewRefPair::New();
  Injector injector(context_provider_.context(), fidl::Clone(context_view_ref),
                    fidl::Clone(target_view_ref));

  const uint32_t kDeviceId = 1;
  fuchsia::ui::input::InputEvent event;
  event.set_pointer({.device_id = kDeviceId});

  injector.OnDeviceAdded(kDeviceId);
  injector.OnEvent(event);
  injector.MarkSceneReady();

  EXPECT_EQ(injector_registry.num_register_calls(), 0u);
  EXPECT_EQ(injector_registry.num_registered(), 0u);
  EXPECT_EQ(injector_registry.num_events_received(), 0u);

  RunLoopUntilIdle();

  EXPECT_EQ(injector_registry.num_register_calls(), 1u);
  EXPECT_EQ(injector_registry.num_registered(), 1u);
  EXPECT_EQ(injector_registry.num_events_received(), 1u);

  // MarkSceneReady() again should have no effect. So the next event should not be sent until the
  // server acks the previous events.
  injector.MarkSceneReady();
  injector.OnEvent(event);

  RunLoopUntilIdle();

  EXPECT_EQ(injector_registry.num_register_calls(), 1u);
  EXPECT_EQ(injector_registry.num_registered(), 1u);
  EXPECT_EQ(injector_registry.num_events_received(), 1u);

  injector_registry.FirePendingCallbacks();

  RunLoopUntilIdle();

  EXPECT_EQ(injector_registry.num_register_calls(), 1u);
  EXPECT_EQ(injector_registry.num_registered(), 1u);
  EXPECT_EQ(injector_registry.num_events_received(), 2u);
}

}  // namespace input::test
