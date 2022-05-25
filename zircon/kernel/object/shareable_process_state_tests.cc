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

  ShareableProcessState state;
  state.IncrementShareCount();

  KernelHandle<EventPairDispatcher> eventpair[2];
  zx_rights_t rights;
  ASSERT_EQ(EventPairDispatcher::Create(&eventpair[0], &eventpair[1], &rights), ZX_OK);

  EXPECT_EQ(state.handle_table().HandleCount(), (uint32_t)0);

  HandleOwner handle_owner;
  handle_owner = Handle::Make(ktl::move(eventpair[0]), rights);
  state.handle_table().AddHandle(ktl::move(handle_owner));

  EXPECT_EQ(state.handle_table().HandleCount(), (uint32_t)1);

  state.DecrementShareCount();

  EXPECT_EQ(state.handle_table().HandleCount(), (uint32_t)0);

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(shareable_process_state_tests)
UNITTEST("IncrementDecrement", IncrementDecrement)
UNITTEST_END_TESTCASE(shareable_process_state_tests, "shareable_process_state",
                      "ShareableProcessState test")
