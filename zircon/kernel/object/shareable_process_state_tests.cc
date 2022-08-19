// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>

#include <object/event_pair_dispatcher.h>
#include <object/shareable_process_state.h>

namespace {

bool IncrementDecrement() {
  BEGIN_TEST;

  // State's share count is initialized to 1.
  ShareableProcessState state;
  // Increment the share count to 2.
  EXPECT_EQ(state.IncrementShareCount(), true);

  KernelHandle<EventPairDispatcher> eventpair[2];
  zx_rights_t rights;
  ASSERT_EQ(EventPairDispatcher::Create(&eventpair[0], &eventpair[1], &rights), ZX_OK);

  EXPECT_EQ(state.handle_table().HandleCount(), (uint32_t)0);

  HandleOwner handle_owner;
  handle_owner = Handle::Make(ktl::move(eventpair[0]), rights);
  state.handle_table().AddHandle(ktl::move(handle_owner));

  // The first decrement should not clear the handle table.
  EXPECT_EQ(state.handle_table().HandleCount(), (uint32_t)1);
  state.DecrementShareCount();
  EXPECT_EQ(state.handle_table().HandleCount(), (uint32_t)1);

  // The second decrement should clear the handle table.
  state.DecrementShareCount();
  EXPECT_EQ(state.handle_table().HandleCount(), (uint32_t)0);

  // Incrementing the share count after the shared state has been destroyed fails.
  EXPECT_EQ(state.IncrementShareCount(), false);

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(shareable_process_state_tests)
UNITTEST("IncrementDecrement", IncrementDecrement)
UNITTEST_END_TESTCASE(shareable_process_state_tests, "shareable_process_state",
                      "ShareableProcessState test")
