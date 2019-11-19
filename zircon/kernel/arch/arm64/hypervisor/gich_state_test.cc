// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>

#include <arch/hypervisor.h>
#include <dev/interrupt/arm_gic_hw_interface.h>

static bool has_pending_interrupt() {
  BEGIN_TEST;

  GichState gich_state;
  ASSERT_EQ(ZX_OK, gich_state.Init());

  IchState ich_state = {};
  ich_state.num_lrs = 2;

  // Test one pending vector.
  ich_state.lr[0] = gic_get_lr_from_vector(false /* hardware */, 0 /* priority */,
                                           InterruptState::PENDING, 1u /* vector */);
  ich_state.lr[1] = 0;
  gich_state.SetAllInterruptStates(&ich_state);
  EXPECT_EQ(true, gich_state.HasPendingInterrupt());

  // Test reset.
  ich_state.lr[0] = 0;
  ich_state.lr[1] = 0;
  gich_state.SetAllInterruptStates(&ich_state);
  EXPECT_EQ(false, gich_state.HasPendingInterrupt());

  // Test one active vector.
  ich_state.lr[0] = gic_get_lr_from_vector(false /* hardware */, 0 /* priority */,
                                           InterruptState::ACTIVE, 1u /* vector */);
  ich_state.lr[1] = 0;
  gich_state.SetAllInterruptStates(&ich_state);
  EXPECT_EQ(false, gich_state.HasPendingInterrupt());

  // Test one active and one pending vector.
  ich_state.lr[0] = gic_get_lr_from_vector(false /* hardware */, 0 /* priority */,
                                           InterruptState::ACTIVE, 1u /* vector */);
  ich_state.lr[1] = gic_get_lr_from_vector(false /* hardware */, 0 /* priority */,
                                           InterruptState::PENDING, 2u /* vector */);
  gich_state.SetAllInterruptStates(&ich_state);
  EXPECT_EQ(true, gich_state.HasPendingInterrupt());

  // Test one pending_and_active vector.
  ich_state.lr[0] = gic_get_lr_from_vector(false /* hardware */, 0 /* priority */,
                                           InterruptState::PENDING_AND_ACTIVE, 1u /* vector */);
  ich_state.lr[1] = 0;
  gich_state.SetAllInterruptStates(&ich_state);
  EXPECT_EQ(true, gich_state.HasPendingInterrupt());

  END_TEST;
}

static bool get_interrupt_state() {
  BEGIN_TEST;

  GichState gich_state;
  ASSERT_EQ(ZX_OK, gich_state.Init());

  // Test initial state
  for (uint32_t i = 0; i != kNumInterrupts - 1; ++i) {
    EXPECT_EQ(InterruptState::INACTIVE, gich_state.GetInterruptState(i));
  }

  IchState ich_state = {};
  ich_state.num_lrs = 2;

  // Test one pending vector.
  ich_state.lr[0] = gic_get_lr_from_vector(false /* hardware */, 0 /* priority */,
                                           InterruptState::PENDING, 1u /* vector */);
  ich_state.lr[1] = 0;
  gich_state.SetAllInterruptStates(&ich_state);
  EXPECT_EQ(InterruptState::PENDING, gich_state.GetInterruptState(1u));
  for (uint32_t i = 0; i != kNumInterrupts - 1; ++i) {
    if (i == 1u) {
      continue;
    }
    EXPECT_EQ(InterruptState::INACTIVE, gich_state.GetInterruptState(i));
  }

  // Test reset.
  ich_state.lr[0] = 0;
  ich_state.lr[1] = 0;
  gich_state.SetAllInterruptStates(&ich_state);
  for (uint32_t i = 0; i != kNumInterrupts - 1; ++i) {
    EXPECT_EQ(InterruptState::INACTIVE, gich_state.GetInterruptState(i));
  }

  // Test other states.
  ich_state.lr[0] = gic_get_lr_from_vector(false /* hardware */, 0 /* priority */,
                                           InterruptState::ACTIVE, 1u /* vector */);
  ich_state.lr[1] = gic_get_lr_from_vector(false /* hardware */, 0 /* priority */,
                                           InterruptState::PENDING_AND_ACTIVE, 2u /* vector */);
  gich_state.SetAllInterruptStates(&ich_state);
  EXPECT_EQ(InterruptState::ACTIVE, gich_state.GetInterruptState(1u));
  EXPECT_EQ(InterruptState::PENDING_AND_ACTIVE, gich_state.GetInterruptState(2u));
  for (uint32_t i = 0; i != kNumInterrupts - 1; ++i) {
    if (i == 1u || i == 2u) {
      continue;
    }
    EXPECT_EQ(InterruptState::INACTIVE, gich_state.GetInterruptState(i));
  }

  END_TEST;
}

// Use the function name as the test name
#define GICH_STATE_UNITTEST(fname) UNITTEST(#fname, fname)

UNITTEST_START_TESTCASE(gich_state)
GICH_STATE_UNITTEST(has_pending_interrupt)
GICH_STATE_UNITTEST(get_interrupt_state)
UNITTEST_END_TESTCASE(gich_state, "gich_state", "Tests for hypervisor GICH state.")
