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

using fuchsia::ui::input::PointerEventPhase;

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

TEST_F(InjectorTest, ShouldWaitUntilADD_ForFirstInjection) {
  MockInjectorRegistry injector_registry(context_provider_);

  const auto [context_control_ref, context_view_ref] = scenic::ViewRefPair::New();
  const auto [target_control_ref, target_view_ref] = scenic::ViewRefPair::New();
  Injector injector(context_provider_.context(), fidl::Clone(context_view_ref),
                    fidl::Clone(target_view_ref));
  injector.MarkSceneReady();
  const uint32_t kDeviceId = 1;
  injector.OnDeviceAdded(kDeviceId);
  RunLoopUntilIdle();
  EXPECT_EQ(injector_registry.num_register_calls(), 1u);
  EXPECT_EQ(injector_registry.num_registered(), 1u);

  fuchsia::ui::input::InputEvent event;

  // First event with MOVE ignored.
  event.set_pointer({.device_id = kDeviceId, .phase = PointerEventPhase::MOVE});
  injector.OnEvent(event);
  RunLoopUntilIdle();
  EXPECT_EQ(injector_registry.num_events_received(), 0u);

  // First event with REMOVE ignored.
  event.set_pointer({.device_id = kDeviceId, .phase = PointerEventPhase::REMOVE});
  injector.OnEvent(event);
  RunLoopUntilIdle();
  EXPECT_EQ(injector_registry.num_events_received(), 0u);
  injector_registry.FirePendingCallbacks();

  // First event with ADD sent.
  event.set_pointer({.device_id = kDeviceId, .phase = PointerEventPhase::ADD});
  injector.OnEvent(event);
  RunLoopUntilIdle();
  EXPECT_EQ(injector_registry.num_events_received(), 1u);

  // Subsequent events sent.
  event.set_pointer({.device_id = kDeviceId, .phase = PointerEventPhase::MOVE});
  injector.OnEvent(event);
  event.set_pointer({.device_id = kDeviceId, .phase = PointerEventPhase::REMOVE});
  injector.OnEvent(event);
  injector_registry.FirePendingCallbacks();
  RunLoopUntilIdle();
  EXPECT_EQ(injector_registry.num_events_received(), 3u);
}

TEST_F(InjectorTest, AfterKilledChannel_ShouldWaitUntilADD_ForRecoveryInjectionAttempt) {
  MockInjectorRegistry injector_registry(context_provider_);

  const auto [context_control_ref, context_view_ref] = scenic::ViewRefPair::New();
  const auto [target_control_ref, target_view_ref] = scenic::ViewRefPair::New();
  Injector injector(context_provider_.context(), fidl::Clone(context_view_ref),
                    fidl::Clone(target_view_ref));
  injector.MarkSceneReady();
  const uint32_t kDeviceId = 1;
  injector.OnDeviceAdded(kDeviceId);
  RunLoopUntilIdle();
  EXPECT_EQ(injector_registry.num_register_calls(), 1u);
  EXPECT_EQ(injector_registry.num_registered(), 1u);

  fuchsia::ui::input::InputEvent event;

  // First event with ADD -> sent.
  event.set_pointer({.device_id = kDeviceId, .phase = PointerEventPhase::ADD});
  injector.OnEvent(event);
  RunLoopUntilIdle();
  EXPECT_EQ(injector_registry.num_events_received(), 1u);

  // Kill the channel. The injector should now try to recover on the next ADD.
  injector_registry.KillAllBindings();
  RunLoopUntilIdle();

  // Non-ADD events should be skipped.
  event.set_pointer({.device_id = kDeviceId, .phase = PointerEventPhase::MOVE});
  injector.OnEvent(event);
  event.set_pointer({.device_id = kDeviceId, .phase = PointerEventPhase::REMOVE});
  injector.OnEvent(event);
  RunLoopUntilIdle();
  EXPECT_EQ(injector_registry.num_events_received(), 1u);

  // Add is sent.
  event.set_pointer({.device_id = kDeviceId, .phase = PointerEventPhase::ADD});
  injector.OnEvent(event);
  RunLoopUntilIdle();
  EXPECT_EQ(injector_registry.num_events_received(), 2u);

  // Subsequent events should be sent.
  event.set_pointer({.device_id = kDeviceId, .phase = PointerEventPhase::MOVE});
  injector.OnEvent(event);
  event.set_pointer({.device_id = kDeviceId, .phase = PointerEventPhase::REMOVE});
  injector.OnEvent(event);
  injector_registry.FirePendingCallbacks();
  RunLoopUntilIdle();
  EXPECT_EQ(injector_registry.num_events_received(), 4u);
}

}  // namespace input::test
