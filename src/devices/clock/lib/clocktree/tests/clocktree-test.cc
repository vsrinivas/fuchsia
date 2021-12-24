// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "clocktree.h"

#include <zircon/types.h>

#include <zxtest/zxtest.h>

#include "baseclock.h"
#include "testclock.h"
#include "types.h"

namespace clk {

namespace {

bool AnyDescendentsEnabled(uint32_t id, cpp20::span<BaseClock*> clocks) {
  for (BaseClock* self : clocks) {
    // Skip any clocks that aren't children of the clock under test.
    if (self->ParentId() != id) {
      continue;
    }

    bool enabled;
    zx_status_t st = self->IsEnabled(&enabled);
    ZX_ASSERT(st == ZX_OK);
    if (enabled) {
      return true;
    }

    bool descendents = AnyDescendentsEnabled(self->Id(), clocks);
    if (descendents) {
      return true;
    }
  }

  return false;
}

void ClockTreeConsistencyCheck(cpp20::span<BaseClock*> clocks, cpp20::span<BaseClock*> leaves) {
  // Validate that a number of constraints on a clock tree are met.

  // If a clock is Enabled, make sure all its parents leading to the root are
  // also enabled.
  for (BaseClock* self : leaves) {
    bool is_enabled = false;
    zx_status_t st = self->IsEnabled(&is_enabled);
    EXPECT_OK(st);
    if (!is_enabled) {
      continue;
    }
    for (uint32_t id = self->Id(); id != kClkNoParent; id = self->ParentId()) {
      self = clocks[id];
      st = self->IsEnabled(&is_enabled);
      EXPECT_OK(st);
      EXPECT_TRUE(is_enabled);
    }
  }

  // If a clock is disabled, make sure that all its children are disabled as well.
  for (BaseClock* self : clocks) {
    bool is_enabled = true;
    zx_status_t st = self->IsEnabled(&is_enabled);
    EXPECT_OK(st);
    if (is_enabled) {
      continue;
    }

    // Since clk[i] is disabled, make sure that all of its descendents are also
    // disabled.
    EXPECT_FALSE(AnyDescendentsEnabled(self->Id(), clocks));
  }
}

}  // namespace

TEST(ClockGateTest, TestGateTrivial) {
  // This is a trivial test that demonstrates Clock Tree functionality.
  // In this example the clock tree has exactly one gate and we validate that
  // calling
  constexpr uint32_t kClkid = 0;
  TestGateClock gate("gate", kClkid, kClkNoParent, false);
  BaseClock* clocks[1] = {&gate};
  Tree tree(clocks, 1);
  bool is_enabled;

  // Make sure that the clock tree reports that the gate clock is disabled
  // as we expect.
  is_enabled = true;
  EXPECT_OK(tree.IsEnabled(kClkid, &is_enabled));
  EXPECT_FALSE(is_enabled);
  is_enabled = true;
  EXPECT_OK(gate.IsEnabled(&is_enabled));
  EXPECT_FALSE(is_enabled);

  // Tell the clock tree to enable the single gate and ensure that it is
  // enabled as expected.
  is_enabled = false;
  EXPECT_OK(tree.Enable(kClkid));
  EXPECT_OK(tree.IsEnabled(kClkid, &is_enabled));
  EXPECT_TRUE(is_enabled);
  is_enabled = false;
  EXPECT_OK(gate.IsEnabled(&is_enabled));
  EXPECT_TRUE(is_enabled);

  // Tell the clock tree to disable the single gate and ensure that it is
  // disabled as expected.
  is_enabled = true;
  EXPECT_OK(tree.Disable(kClkid));
  EXPECT_OK(tree.IsEnabled(kClkid, &is_enabled));
  EXPECT_FALSE(is_enabled);
  is_enabled = true;
  EXPECT_OK(gate.IsEnabled(&is_enabled));
  EXPECT_FALSE(is_enabled);
}

TEST(ClockGateTest, TestGateParent) {
  // Create two clock gates with a parent child relationship and ensure that ungating the
  // child causes the parent to be ungated as well.
  // Clock heirarchy is as follows:
  //   [A] --> [B]
  //
  constexpr uint32_t kClkChild = 0;
  constexpr uint32_t kClkParent = 1;
  constexpr uint32_t kClkCount = 2;

  TestGateClock child("child", kClkChild, kClkParent, false);
  TestGateClock parent("parent", kClkParent, kClkNoParent, false);
  BaseClock* clocks[kClkCount] = {&child, &parent};
  Tree tree(clocks, kClkCount);

  // Enable the child.
  EXPECT_OK(tree.Enable(kClkChild));

  // Ensure the parent is enabled as well.
  bool is_enabled = false;
  EXPECT_OK(tree.IsEnabled(kClkParent, &is_enabled));
  EXPECT_TRUE(is_enabled);
}

TEST(ClockGateTest, TestGateUnsupported) {
  // Create three clocks that form a parent child chain as follows:
  //   [A] --> [B] --> [C]
  // Where C is the root and B does not support gating/ungating.
  // Calling Enable on A should enable C as well even if B does not support
  // gating/ungating.
  constexpr uint32_t kChild = 0;
  constexpr uint32_t kMiddle = 1;
  constexpr uint32_t kRoot = 2;
  constexpr uint32_t kCount = 3;

  TestGateClock A("a", kChild, kMiddle, false);
  TestNullClock B("b", kMiddle, kRoot);
  TestGateClock C("c", kRoot, kClkNoParent, false);
  BaseClock* clocks[kCount] = {&A, &B, &C};
  Tree tree(clocks, kCount);

  bool is_enabled = true;
  EXPECT_OK(tree.IsEnabled(kChild, &is_enabled));
  EXPECT_FALSE(is_enabled);

  is_enabled = true;
  EXPECT_OK(tree.IsEnabled(kRoot, &is_enabled));
  EXPECT_FALSE(is_enabled);

  EXPECT_OK(tree.Enable(kChild));

  is_enabled = true;
  EXPECT_OK(tree.IsEnabled(kChild, &is_enabled));
  EXPECT_TRUE(is_enabled);

  is_enabled = true;
  EXPECT_OK(tree.IsEnabled(kRoot, &is_enabled));
  EXPECT_TRUE(is_enabled);
}

TEST(ClockGateTest, TestGateUnused) {
  // Create a parent and a child gate clock and make sure that the parent
  // becomes gated when it has no more votes.
  //   [A] --> [B]
  constexpr uint32_t kChild = 0;
  constexpr uint32_t kParent = 1;
  constexpr uint32_t kCount = 2;
  TestGateClock child("child", kChild, kParent, false);
  TestGateClock parent("parent", kParent, kClkNoParent, false);
  BaseClock* clocks[kCount] = {&child, &parent};
  Tree tree(clocks, kCount);

  // Make sure the child is disabled to start.
  bool is_enabled = true;
  EXPECT_OK(tree.IsEnabled(kChild, &is_enabled));
  EXPECT_FALSE(is_enabled);

  // Make sure the parent is disabled to start.
  is_enabled = true;
  EXPECT_OK(tree.IsEnabled(kParent, &is_enabled));
  EXPECT_FALSE(is_enabled);

  // Enable the child and make sure that the child and parent are both enabled.
  is_enabled = false;
  EXPECT_OK(tree.Enable(kChild));
  EXPECT_OK(tree.IsEnabled(kChild, &is_enabled));
  EXPECT_TRUE(is_enabled);

  is_enabled = false;
  EXPECT_OK(tree.IsEnabled(kParent, &is_enabled));
  EXPECT_TRUE(is_enabled);

  // Disabling the child means that the vote count for the parent drops to zero
  // meaning that it should be disabled as well.
  is_enabled = true;
  EXPECT_OK(tree.Disable(kChild));
  EXPECT_OK(tree.IsEnabled(kParent, &is_enabled));
  EXPECT_FALSE(is_enabled);
}

TEST(ClockGateTest, TestGateMultiChild) {
  // Assume a parent gate with two child gate clocks, all starting in the disabled
  // state as follows:
  //
  // [A] --+
  //       |
  //       +--> [C]
  //       |
  // [B] --+
  //
  // Enabling either of the children should enable the parent. If both children
  // are enabled then one child is disabled, the parent should remain enabled.
  // If both children are disabled, the parent should be disabled as well.
  constexpr uint32_t kFirstChild = 0;
  constexpr uint32_t kSecondChild = 1;
  constexpr uint32_t kParent = 2;
  constexpr uint32_t kCount = 3;
  TestGateClock first("first child", kFirstChild, kParent, false);
  TestGateClock second("second child", kSecondChild, kParent, false);
  TestGateClock parent("first child", kParent, kClkNoParent, false);

  BaseClock* clocks[kCount] = {&first, &second, &parent};
  Tree tree(clocks, kCount);

  // Enable one of the children and make sure the parent is enabled.
  EXPECT_OK(tree.Enable(kFirstChild));

  bool is_enabled = false;
  EXPECT_OK(tree.IsEnabled(kParent, &is_enabled));
  EXPECT_TRUE(is_enabled);

  // Enable the second child.
  EXPECT_OK(tree.Enable(kSecondChild));

  // Disable the first child.
  EXPECT_OK(tree.Disable(kFirstChild));

  // Since the second child also has a dependency on the parent, make sure the parent
  // doesn't get turned off.
  is_enabled = false;
  EXPECT_OK(tree.IsEnabled(kParent, &is_enabled));
  EXPECT_TRUE(is_enabled);

  // Shut down the second child and ensure that the parent now shuts off because it
  // has no more dependents.
  EXPECT_OK(tree.Disable(kSecondChild));
  is_enabled = true;
  EXPECT_OK(tree.IsEnabled(kParent, &is_enabled));
  EXPECT_FALSE(is_enabled);
}

TEST(ClockGateTest, TestGateUnwind) {
  // Consider a chain of three clocks A, B and C as follows:
  //
  //   [A] --> [B] --> [C]
  //
  // If we attempt to enable A we normally expect B and C to be enabled on our
  // behalf starting with the root (i.e. Enable C, Enable B, Enable A).
  // However if a call fails somewhere in the chain we need to make sure we
  // unwind all the clocks above us that we've enabled.
  // In this test, B will fail to enable and we will ensure that C is not
  // left in an enabled state.
  constexpr uint32_t kClkChild = 0;
  constexpr uint32_t kClkFailer = 1;
  constexpr uint32_t kClkRoot = 2;
  constexpr uint32_t kClkCount = 3;

  TestGateClock child("child", kClkChild, kClkFailer, false);
  TestFailClock failer("failer", kClkFailer, kClkRoot);
  TestGateClock root("root", kClkRoot, kClkNoParent, false);

  BaseClock* clocks[kClkCount] = {&child, &failer, &root};

  Tree tree(clocks, kClkCount);

  // Try to enable the "child" clock. This should fail because its parent
  // reports an error.
  EXPECT_NOT_OK(tree.Enable(kClkChild));

  // Since there was a failure in the chain, make sure that we didn't actually
  // enable either of the gates.

  bool is_enabled = true;
  EXPECT_OK(tree.IsEnabled(kClkChild, &is_enabled));
  EXPECT_FALSE(is_enabled);

  is_enabled = true;
  EXPECT_OK(tree.IsEnabled(kClkRoot, &is_enabled));
  EXPECT_FALSE(is_enabled);

  is_enabled = true;
  EXPECT_OK(child.IsEnabled(&is_enabled));
  EXPECT_FALSE(is_enabled);

  is_enabled = true;
  EXPECT_OK(root.IsEnabled(&is_enabled));
  EXPECT_FALSE(is_enabled);
}

TEST(ClockGateTest, TestExtraneousEnableDisable) {
  // Make sure that calling Enable multiple times on a clock that is
  // already enabled does not actually call Enable on the
  // hardware.
  // I.e. Calling "Enable" 5 times in a row should only result in the Enable
  // bits being set once for the underlying hardware.
  constexpr uint32_t kClkTest = 0;
  constexpr uint32_t kClkCount = 1;
  constexpr size_t kAttemptCount = 5;

  TestGateClock test("test-clock", kClkTest, kClkNoParent, false);

  BaseClock* clocks[kClkCount] = {&test};

  Tree tree(clocks, kClkCount);

  // Enable the clock more than once. We expect this to create 5 votes for this
  // meaning that we must call disable at least 5 times before this clock is
  // disabled however we only expect disable to be called on the underlying
  // clock hardware once.
  for (size_t i = 0; i < kAttemptCount; ++i) {
    EXPECT_OK(tree.Enable(kClkTest));
  }

  EXPECT_EQ(test.EnableCount(), 1);

  // If we try to disable an already disabled clock, make sure that we assert.
  for (size_t i = 0; i < kAttemptCount; ++i) {
    EXPECT_OK(tree.Disable(kClkTest));
  }

  EXPECT_EQ(test.DisableCount(), 1);

  // Too many disables should yield an error.
  EXPECT_NOT_OK(tree.Disable(kClkTest));
}

TEST(ClockMuxTest, TestReparentTrivial) {
  // Ask a clock who its input is. Change the parent to somebody else and try
  // again. Observe that the change was successful.
  constexpr uint32_t kClkChild = 0;
  constexpr uint32_t kClkFirstParent = 1;
  constexpr uint32_t kClkSecondParent = 2;
  constexpr uint32_t kClkCount = 3;

  constexpr uint32_t parents[] = {kClkFirstParent, kClkSecondParent};
  TestMuxClockTrivial t("trivial mux", kClkChild, parents, std::size(parents));
  TestNullClock p1("parent 1", kClkFirstParent, kClkNoParent);
  TestNullClock p2("parent 2", kClkSecondParent, kClkNoParent);

  BaseClock* clocks[kClkCount] = {&t, &p1, &p2};

  Tree tree(clocks, kClkCount);

  // By default, clocks are parented to their first input.
  uint32_t input = std::numeric_limits<uint32_t>::max();

  EXPECT_OK(tree.GetInput(kClkChild, &input));
  EXPECT_EQ(input, 0);
  EXPECT_EQ(t.ParentId(), kClkFirstParent);

  // Try reparenting, make sure it works.
  EXPECT_OK(tree.SetInput(kClkChild, 1));
  EXPECT_OK(tree.GetInput(kClkChild, &input));
  EXPECT_EQ(input, 1);
  EXPECT_EQ(t.ParentId(), kClkSecondParent);

  uint32_t num_inputs;
  EXPECT_OK(tree.GetNumInputs(kClkChild, &num_inputs));
  EXPECT_EQ(num_inputs, 2);

  // Try to reparent to a clock that's out of range and ensure that it doesn't
  // work.
  uint32_t old_input, new_input;
  EXPECT_OK(tree.GetInput(kClkChild, &old_input));
  EXPECT_NOT_OK(tree.SetInput(kClkChild, num_inputs));
  EXPECT_OK(tree.GetInput(kClkChild, &new_input));
  EXPECT_EQ(old_input, new_input);
}

TEST(ClockMuxTest, TestReparentEnableDisable) {
  // If a clock is enabled and it is reparented to a new clock, it should move
  // its vote from the old parent to the new parent.
  // This ensures that the old parent is disabled when it has no more
  // dependencies.

  constexpr uint32_t kClkChild = 0;
  constexpr uint32_t kClkFirstParent = 1;
  constexpr uint32_t kClkSecondParent = 2;
  constexpr uint32_t kClkCount = 3;

  constexpr uint32_t parents[] = {kClkFirstParent, kClkSecondParent};
  TestMuxClockTrivial t("mux under test", kClkChild, parents, std::size(parents));
  TestGateClock p1("parent 1", kClkFirstParent, kClkNoParent);
  TestGateClock p2("parent 2", kClkSecondParent, kClkNoParent);

  BaseClock* clocks[kClkCount] = {&t, &p1, &p2};

  Tree tree(clocks, kClkCount);

  // This should Enable P1
  EXPECT_OK(tree.Enable(kClkChild));

  // Ensure that Child reports that it is enabled.
  bool is_enabled = false;
  EXPECT_OK(tree.IsEnabled(kClkChild, &is_enabled));
  EXPECT_TRUE(is_enabled);

  // Ensure that P1 reports that it is enabled.
  is_enabled = false;
  EXPECT_OK(tree.IsEnabled(kClkFirstParent, &is_enabled));
  EXPECT_TRUE(is_enabled);

  // Ensure that P2 reports that it is disabled.
  is_enabled = true;
  EXPECT_OK(tree.IsEnabled(kClkSecondParent, &is_enabled));
  EXPECT_FALSE(is_enabled);

  // Now reparent the child clock to the second parent and validate that the
  // first parent becomes disabled and the second parent becomes enabled.
  EXPECT_OK(tree.SetInput(kClkChild, 1));

  // Make sure that the child still reports that it's enabled.
  is_enabled = false;
  EXPECT_OK(tree.IsEnabled(kClkChild, &is_enabled));
  EXPECT_TRUE(is_enabled);

  // The first parent has no more refs, so it should be disabled.
  is_enabled = true;
  EXPECT_OK(tree.IsEnabled(kClkFirstParent, &is_enabled));
  EXPECT_FALSE(is_enabled);

  // The second parent just picked up a ref so it should be enabled.
  is_enabled = false;
  EXPECT_OK(tree.IsEnabled(kClkSecondParent, &is_enabled));
  EXPECT_TRUE(is_enabled);
}

TEST(ClockMuxTest, TestReparentFail) {
  // What happens if we tell the clock hardware to reparent and the operation fails?
  // Ensure that if the hardware fails to reparent we don't change the clock
  // topology.

  constexpr uint32_t kClkChild = 0;
  constexpr uint32_t kClkP1 = 1;
  constexpr uint32_t kClkP2 = 2;
  constexpr uint32_t kClkCount = 3;

  constexpr uint32_t parents[] = {kClkP1, kClkP2};
  TestMuxClockFail child("child", kClkChild, parents, std::size(parents));
  TestGateClock p1("first parent", kClkP1, kClkNoParent);
  TestGateClock p2("second parent", kClkP2, kClkNoParent);

  BaseClock* clocks[] = {&child, &p1, &p2};

  Tree tree(clocks, kClkCount);

  // Initially, all clocks are disabled and child's input is P1. Calling enable
  // should enable child and p1.
  EXPECT_OK(tree.Enable(kClkChild));

  bool is_enabled = false;
  EXPECT_OK(p1.IsEnabled(&is_enabled));
  EXPECT_TRUE(is_enabled);
  EXPECT_OK(p2.IsEnabled(&is_enabled));
  EXPECT_FALSE(is_enabled);

  // This test mux is designed to always fail when tryign to set the input.
  // If the set input op fails, we should make sure we don't disturb the clock
  // topology.
  EXPECT_NOT_OK(tree.SetInput(kClkChild, 1));

  is_enabled = false;
  EXPECT_OK(p1.IsEnabled(&is_enabled));
  EXPECT_TRUE(is_enabled);
  EXPECT_OK(p2.IsEnabled(&is_enabled));
  EXPECT_FALSE(is_enabled);
}

TEST(ClockMuxTest, TestReparentMultiRef) {
  // Consider the following clock topology:
  // [G1] --+----[G3]
  //        |
  //        +--\
  //            +-- [M1]
  // [G2]------/
  //
  constexpr uint32_t kClkG1 = 0;
  constexpr uint32_t kClkG2 = 1;
  constexpr uint32_t kClkG3 = 2;
  constexpr uint32_t kClkM1 = 3;
  constexpr uint32_t kClkCount = 4;

  TestGateClock g1("g1", kClkG1, kClkNoParent);
  TestGateClock g2("g2", kClkG2, kClkNoParent);
  TestGateClock g3("g3", kClkG3, kClkG1);
  constexpr uint32_t parents[] = {kClkG1, kClkG2};
  TestMuxClockTrivial m1("m1", kClkM1, parents, std::size(parents));

  BaseClock* clocks[] = {&g1, &g2, &g3, &m1};
  BaseClock* leaves[] = {&m1, &g3};
  Tree tree(clocks, kClkCount);

  // Reparent with everything turned off.
  EXPECT_OK(tree.SetInput(kClkM1, 1));
  ClockTreeConsistencyCheck(clocks, leaves);

  EXPECT_OK(tree.SetInput(kClkM1, 0));
  ClockTreeConsistencyCheck(clocks, leaves);

  // Turn on M1 and reparent it.
  EXPECT_OK(tree.Enable(kClkM1));
  ClockTreeConsistencyCheck(clocks, leaves);
  EXPECT_OK(tree.SetInput(kClkM1, 1));
  ClockTreeConsistencyCheck(clocks, leaves);

  // Turn off M1, and turn on G3
  EXPECT_OK(tree.Disable(kClkM1));
  EXPECT_OK(tree.Enable(kClkG3));
  ClockTreeConsistencyCheck(clocks, leaves);

  // Reparent M1 and make sure things stay consistent.
  EXPECT_OK(tree.SetInput(kClkM1, 1));
  ClockTreeConsistencyCheck(clocks, leaves);
  EXPECT_OK(tree.SetInput(kClkM1, 0));
  ClockTreeConsistencyCheck(clocks, leaves);

  // Turn both M1 and G3 on and make sure things stay consistent.
  EXPECT_OK(tree.Enable(kClkM1));
  ClockTreeConsistencyCheck(clocks, leaves);
  EXPECT_OK(tree.SetInput(kClkM1, 1));
  ClockTreeConsistencyCheck(clocks, leaves);
  EXPECT_OK(tree.SetInput(kClkM1, 0));
  ClockTreeConsistencyCheck(clocks, leaves);
}

}  // namespace clk
