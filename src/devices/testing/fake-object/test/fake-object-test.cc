// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake-object/object.h>
#include <lib/zx/event.h>
#include <lib/zx/vmo.h>
#include <zircon/rights.h>

#include <array>
#include <climits>  // PAGE_SIZE
#include <utility>

#include <fbl/ref_ptr.h>
#include <zxtest/zxtest.h>

#include "fbl/algorithm.h"
#include "lib/zx/time.h"
#include "zircon/limits.h"
#include "zircon/types.h"

namespace {

class FakeObject : public zxtest::Test {
 protected:
  void TearDown() final { FakeHandleTable().Clear(); }
};

TEST_F(FakeObject, DuplicateHandle) {
  // Setup, create a fake bti, make sure it is valid:
  zx_handle_t obj = ZX_HANDLE_INVALID;
  zx_handle_t obj_dup = ZX_HANDLE_INVALID;

  EXPECT_OK(fake_object_create(&obj));
  EXPECT_NE(obj, ZX_HANDLE_INVALID);

  // Duplicate the handle, make sure it is valid and the same object:
  EXPECT_OK(zx_handle_duplicate(obj, 0, &obj_dup));
  EXPECT_EQ(2, FakeHandleTable().size());
  EXPECT_EQ(fake_object_get_koid(obj), fake_object_get_koid(obj_dup));

  EXPECT_OK(zx_handle_close(obj));
  EXPECT_OK(zx_handle_close(obj_dup));
  EXPECT_EQ(0u, FakeHandleTable().size());
}

TEST_F(FakeObject, DuplicateRealHandle) {
  // Setup, create an event and duplicate it, to make sure that still works:
  zx::event event, event_dup;
  ASSERT_OK(zx::event::create(0u, &event), "Error during event create");
  EXPECT_OK(event.duplicate(ZX_RIGHT_SAME_RIGHTS, &event_dup));

  // The ZX_EVENT_SIGNALED bit is guaranteed to be 0 when we create the object.
  // Now signal the original event:
  ASSERT_OK(event.signal(0u, ZX_EVENT_SIGNALED));
  zx_signals_t pending;
  // Now wait for that signal on the duplicated version:
  EXPECT_OK(event_dup.wait_one(ZX_EVENT_SIGNALED, zx::time(0), &pending));
  EXPECT_EQ(pending & ZX_EVENT_SIGNALED, ZX_EVENT_SIGNALED, "Error during wait call");
}

TEST_F(FakeObject, ReplaceHandle) {
  zx_handle_t obj_hnd = ZX_HANDLE_INVALID;
  zx_handle_t obj_hnd_repl = ZX_HANDLE_INVALID;
  fbl::RefPtr<Object> obj;

  EXPECT_OK(fake_object_create(&obj_hnd));
  zx_koid_t original_koid = fake_object_get_koid(obj_hnd);
  EXPECT_OK(zx_handle_replace(obj_hnd, 0, &obj_hnd_repl));
  EXPECT_EQ(ZX_ERR_NOT_FOUND, FakeHandleTable().Get(obj_hnd, &obj));
  EXPECT_EQ(original_koid, fake_object_get_koid(obj_hnd_repl));

  EXPECT_OK(zx_handle_close(obj_hnd_repl));
  EXPECT_EQ(0u, FakeHandleTable().size());
}

TEST_F(FakeObject, ReplaceRealHandle) {
  zx::event event, event_repl;
  ASSERT_OK(zx::event::create(0u, &event), "Error during event create");

  zx_handle_t old_hnd = event.get();
  ASSERT_OK(event.replace(0, &event_repl));
  ASSERT_EQ(event.get(), ZX_HANDLE_INVALID);
  ASSERT_NE(old_hnd, event_repl.get());
}

TEST_F(FakeObject, HandleClose) {
  zx_handle_t obj = ZX_HANDLE_INVALID;
  EXPECT_OK(fake_object_create(&obj));
  EXPECT_NE(obj, ZX_HANDLE_INVALID);
  EXPECT_EQ(1u, FakeHandleTable().size());

  EXPECT_OK(zx_handle_close(obj));
  EXPECT_EQ(0u, FakeHandleTable().size());
}

TEST(FakeObject, HandleCloseMany) {
  // Ensure other test state was cleaned up.
  ASSERT_EQ(0, FakeHandleTable().size());
  std::array<zx_handle_t, 4> handles = {ZX_HANDLE_INVALID};

  EXPECT_OK(fake_object_create(&handles[0]));
  EXPECT_OK(zx_event_create(0, &handles[1]));
  // [2] will be ZX_HANDLE_INVALID
  EXPECT_OK(zx_event_create(0, &handles[3]));

  ASSERT_NO_DEATH([handles]() { EXPECT_OK(zx_handle_close_many(handles.data(), handles.size())); });
}

TEST_F(FakeObject, WaitMany) {
  std::array<zx_wait_item_t, 3> items;
  EXPECT_OK(zx_event_create(0, &items[0].handle));
  EXPECT_OK(zx_event_create(0, &items[1].handle));
  EXPECT_OK(fake_object_create(&items[2].handle));

  // This should assert due to a fake handle being in the list of wait items.
  ASSERT_DEATH(
      ([&items] { ASSERT_OK(zx_object_wait_many(items.data(), items.size(), ZX_TIME_INFINITE)); }),
      "");

  // This should behave normally due to being real events and simply return the timeout error.
  ASSERT_NO_DEATH(([&items] {
                    ASSERT_EQ(zx_object_wait_many(items.data(), items.size() - 1,
                                                  zx_deadline_after(ZX_MSEC(1))),
                              ZX_ERR_TIMED_OUT);
                  }),
                  "");
}

constexpr zx_handle_t kPotentialHandle = 1;
TEST_F(FakeObject, DuplicateInvalidHandle) {
  zx_handle_t obj = ZX_HANDLE_INVALID;
  zx_handle_t obj_dup = ZX_HANDLE_INVALID;
  // Duplicating an invalid handle should return an error but not die.
  ASSERT_NO_DEATH(([obj, &obj_dup]() { EXPECT_NOT_OK(zx_handle_duplicate(obj, 0, &obj_dup)); }));

  // However, a real handle will just return an error:
  obj = kPotentialHandle;
  ASSERT_NO_DEATH(
      ([obj, &obj_dup]() { EXPECT_NOT_OK(REAL_SYSCALL(zx_handle_duplicate)(obj, 0, &obj_dup)); }));
}

struct fake_object_data_t {
  zx_koid_t koid;
  bool seen;
};

// Ensure objects are walked in-order when ForEach is called.
TEST_F(FakeObject, ForEach) {
  std::array<fake_object_data_t, 16> fake_objects = {};
  for (auto& fake_obj : fake_objects) {
    zx_handle_t handle = ZX_HANDLE_INVALID;
    ASSERT_OK(fake_object_create(&handle));
    fake_obj.koid = fake_object_get_koid(handle);
  }

  // Walk the objects ensuring the koids match the objects created earlier.
  size_t idx = 0;
  FakeHandleTable().ForEach(HandleType::BASE, [&idx, &fake_objects](Object* obj) -> bool {
    auto& fake_object = fake_objects[idx];
    if (fake_object.koid == obj->get_koid()) {
      fake_object.seen = true;
    }
    idx++;
    return true;
  });

  // Ensure every object was seen in the ForEach.
  for (auto& fake_object : fake_objects) {
    ASSERT_TRUE(fake_object.seen);
  }
}  // namespace

}  // namespace
