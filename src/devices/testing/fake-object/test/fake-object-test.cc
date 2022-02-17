// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake-object/object.h>
#include <lib/zx/event.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/process.h>
#include <lib/zx/status.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <zircon/limits.h>
#include <zircon/rights.h>
#include <zircon/types.h>

#include <array>
#include <climits>  // PAGE_SIZE
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/ref_ptr.h>
#include <zxtest/zxtest.h>

namespace fake_object {

class FakeObject : public zxtest::Test {
 protected:
  // Catch handles leaked through tests to ensure the library itself doesn't leak any.
  void TearDown() final { ZX_ASSERT(fake_object::FakeHandleTable().size() == 0); }
};

// By default a base |Object| should return ZX_ERR_NOT_SUPPORTED for
// all intercepted object syscalls. This tests that the dispatch for
// the fake syscall routing works for syscalls other tests don't exercise.
// They are organized into individual tests to make it easier to tell if a
// specific syscall is broken.
TEST_F(FakeObject, ShimGetInfo) {
  zx::status res = fake_object_create();
  ASSERT_OK(res.status_value());
  ASSERT_STATUS(zx_object_get_info(res.value(), 0, nullptr, 0, nullptr, nullptr),
                ZX_ERR_NOT_SUPPORTED);
  EXPECT_OK(zx_handle_close(res.value()));
}

TEST_F(FakeObject, ShimGetProperty) {
  zx::status res = fake_object_create();
  ASSERT_OK(res.status_value());
  ASSERT_STATUS(zx_object_get_property(res.value(), 0, nullptr, 0), ZX_ERR_NOT_SUPPORTED);
  EXPECT_OK(zx_handle_close(res.value()));
}

TEST_F(FakeObject, ShimSetProfile) {
  zx::status res = fake_object_create();
  ASSERT_OK(res.status_value());
  ASSERT_STATUS(zx_object_set_profile(res.value(), 0, 0), ZX_ERR_NOT_SUPPORTED);
  EXPECT_OK(zx_handle_close(res.value()));
}

TEST_F(FakeObject, ShimSetProperty) {
  zx::status res = fake_object_create();
  ASSERT_OK(res.status_value());
  ASSERT_STATUS(zx_object_set_property(res.value(), 0, nullptr, 0), ZX_ERR_NOT_SUPPORTED);
  EXPECT_OK(zx_handle_close(res.value()));
}

TEST_F(FakeObject, ShimSignal) {
  zx::status res = fake_object_create();
  ASSERT_OK(res.status_value());
  ASSERT_STATUS(zx_object_signal(res.value(), 0, 0), ZX_ERR_NOT_SUPPORTED);
  EXPECT_OK(zx_handle_close(res.value()));
}

TEST_F(FakeObject, ShimSignalPeer) {
  zx::status res = fake_object_create();
  ASSERT_OK(res.status_value());
  ASSERT_STATUS(zx_object_signal_peer(res.value(), 0, 0), ZX_ERR_NOT_SUPPORTED);
  EXPECT_OK(zx_handle_close(res.value()));
}

TEST_F(FakeObject, ShimWaitOne) {
  zx::status res = fake_object_create();
  ASSERT_OK(res.status_value());
  ASSERT_STATUS(zx_object_wait_one(res.value(), 0, 0, nullptr), ZX_ERR_NOT_SUPPORTED);
  EXPECT_OK(zx_handle_close(res.value()));
}

TEST_F(FakeObject, ShimWaitAsync) {
  zx::status res = fake_object_create();
  ASSERT_OK(res.status_value());
  ASSERT_STATUS(zx_object_wait_one(res.value(), 0, 0, nullptr), ZX_ERR_NOT_SUPPORTED);
  EXPECT_OK(zx_handle_close(res.value()));
}

TEST_F(FakeObject, HandleValidityCheck) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(0, 0, &vmo));
  ASSERT_FALSE(HandleTable::IsValidFakeHandle(vmo.get()));

  auto result = fake_object_create();
  ASSERT_TRUE(result.is_ok());
  ASSERT_TRUE(HandleTable::IsValidFakeHandle(result.value()));
  EXPECT_OK(zx_handle_close(result.value()));
}

TEST_F(FakeObject, Get) {
  EXPECT_EQ(FakeHandleTable().size(), 0);
  zx::status<zx_handle_t> obj = fake_object_create();
  EXPECT_OK(obj.status_value());

  zx::status<fbl::RefPtr<Object>> getter = FakeHandleTable().Get(obj.value());
  EXPECT_TRUE(getter.is_ok());

  EXPECT_EQ(FakeHandleTable().size(), 1);
  EXPECT_OK(zx_handle_close(obj.value()));
}

TEST_F(FakeObject, DuplicateHandle) {
  // Setup, create a fake bti, make sure it is valid:
  zx::status<zx_handle_t> obj = fake_object_create();
  EXPECT_OK(obj.status_value());

  // Duplicate the handle, make sure it is valid and the same object:
  zx_handle_t obj_dup = ZX_HANDLE_INVALID;
  EXPECT_OK(zx_handle_duplicate(obj.value(), 0, &obj_dup));
  EXPECT_EQ(2, fake_object::FakeHandleTable().size());
  zx::status obj_koid = fake_object_get_koid(obj.value());
  zx::status obj_dup_koid = fake_object_get_koid(obj_dup);
  EXPECT_OK(obj_koid.status_value());
  EXPECT_OK(obj_dup_koid.status_value());
  EXPECT_EQ(obj_koid.value(), obj_dup_koid.value());

  EXPECT_OK(zx_handle_close(obj.value()));
  EXPECT_OK(zx_handle_close(obj_dup));
  EXPECT_EQ(0u, fake_object::FakeHandleTable().size());
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
  zx::status<zx_handle_t> obj = fake_object_create();
  zx_handle_t obj_repl_hnd = ZX_HANDLE_INVALID;

  EXPECT_OK(obj.status_value());
  zx::status<zx_koid_t> original_koid = fake_object_get_koid(obj.value());
  EXPECT_OK(original_koid.status_value());
  EXPECT_OK(zx_handle_replace(obj.value(), 0, &obj_repl_hnd));
  EXPECT_STATUS(fake_object::FakeHandleTable().Get(obj.value()).status_value(), ZX_ERR_NOT_FOUND);
  EXPECT_EQ(original_koid, fake_object_get_koid(obj_repl_hnd));

  EXPECT_OK(zx_handle_close(obj_repl_hnd));
  EXPECT_EQ(0u, fake_object::FakeHandleTable().size());
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
  zx::status<zx_handle_t> obj = fake_object_create();
  EXPECT_OK(obj.status_value());
  EXPECT_NE(obj.value(), ZX_HANDLE_INVALID);
  EXPECT_EQ(1u, fake_object::FakeHandleTable().size());

  EXPECT_OK(zx_handle_close(obj.value()));
  EXPECT_EQ(0u, fake_object::FakeHandleTable().size());
}

TEST(FakeObject, HandleCloseMany) {
  // Ensure other test state was cleaned up.
  ASSERT_EQ(0, fake_object::FakeHandleTable().size());
  std::array<zx_handle_t, 4> handles = {ZX_HANDLE_INVALID};

  zx::status<zx_handle_t> obj_res = fake_object_create();
  EXPECT_OK(obj_res.status_value());
  handles[0] = obj_res.value();
  EXPECT_OK(zx_event_create(0, &handles[1]));
  // [2] will be ZX_HANDLE_INVALID
  EXPECT_OK(zx_event_create(0, &handles[3]));

  ASSERT_NO_DEATH([handles]() { EXPECT_OK(zx_handle_close_many(handles.data(), handles.size())); });
}

TEST_F(FakeObject, WaitMany) {
  std::array<zx_wait_item_t, 3> items;
  EXPECT_OK(zx_event_create(0, &items[0].handle));
  EXPECT_OK(zx_event_create(0, &items[1].handle));
  zx::status<zx_handle_t> obj_res = fake_object_create();
  EXPECT_OK(obj_res.status_value());
  items[2].handle = obj_res.value();

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

  for (auto& item : items) {
    EXPECT_OK(zx_handle_close(item.handle));
  }
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

// Ensure objects are walked in-order when ForEach is called.
TEST_F(FakeObject, ForEach) {
  std::unordered_map<zx_koid_t, bool> fake_objects;
  for (int i = 0; i < 16; i++) {
    zx::status<zx_handle_t> obj_res = fake_object_create();
    ASSERT_TRUE(obj_res.is_ok());
    zx::status<zx_koid_t> koid_res = fake_object_get_koid(obj_res.value());
    ASSERT_TRUE(koid_res.is_ok());
    fake_objects[koid_res.value()] = false;
  }

  // Walk the objects ensuring that we've seen all the koids we inserted before.
  FakeHandleTable().ForEach(ZX_OBJ_TYPE_NONE, [&fake_objects](Object* obj) -> bool {
    fake_objects[obj->get_koid()] = true;
    return true;
  });

  // Ensure every object was seen in the ForEach.
  for (auto& pair : fake_objects) {
    EXPECT_TRUE(pair.second);
  }

  // Clean up the 16 objects.
  FakeHandleTable().Clear();
}

// Ensure fake objects can be transmitted over a channel.
TEST_F(FakeObject, Channel) {
  zx::channel in, out;
  ASSERT_OK(zx::channel::create(0, &in, &out));
  auto result = fake_object_create();
  ASSERT_TRUE(result.is_ok());
  ASSERT_TRUE(HandleTable::IsValidFakeHandle(result.value()));
  ASSERT_OK(in.write(0, nullptr, 0, &result.value(), 1));

  zx::handle handle;
  uint32_t actual_handles = 0;
  ASSERT_OK(out.read(0, nullptr, handle.reset_and_get_address(), 0, 1, nullptr, &actual_handles));
  ASSERT_EQ(actual_handles, 1);
  ASSERT_TRUE(HandleTable::IsValidFakeHandle(handle.get()));
}

// Verify that we drop type testing for fake objects which is a requirement for
// working with FIDL bindings.
TEST_F(FakeObject, ChannelEtc) {
  zx::channel in, out;
  ASSERT_OK(zx::channel::create(0, &in, &out));
  zx_obj_type_t test_type = ZX_OBJ_TYPE_BTI;
  auto result = fake_object_create_typed(test_type);
  ASSERT_TRUE(result.is_ok());
  zx_handle_t fake_obj = result.value();
  ASSERT_TRUE(HandleTable::IsValidFakeHandle(fake_obj));

  // We need some real objects to toss into the channel to verify we don't break them.
  zx::vmo vmo;
  zx::event event;
  zx::eventpair ep1, ep2;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), /*options=*/0, &vmo));
  ASSERT_OK(zx::event::create(/*options=*/0, &event));
  ASSERT_OK(zx::eventpair::create(/*options=*/0, &ep1, &ep2));

  // The default operation is to move handles over the channel, but this test
  // uses duplication so that we don't invalidate the test handles before the
  // final run where we intend to be fully successful.
  std::array<zx_handle_disposition_t, 5> wr_handles{
      zx_handle_disposition_t{
          .operation = ZX_HANDLE_OP_DUPLICATE,
          .handle = vmo.get(),
          .type = ZX_OBJ_TYPE_VMO,
          .rights = ZX_RIGHT_SAME_RIGHTS,
      },
      {
          .operation = ZX_HANDLE_OP_DUPLICATE,
          .handle = event.get(),
          .type = ZX_OBJ_TYPE_EVENT,
          .rights = ZX_RIGHT_SAME_RIGHTS,
      },
      // This is the fake, which will attempt to masquerade as a handle to a BTI.
      {
          .operation = ZX_HANDLE_OP_DUPLICATE,
          .handle = fake_obj,
          .type = test_type,
          .rights = ZX_RIGHT_SAME_RIGHTS,
      },
      // Intentionally set this eventpair to the wrong type so we know type
      // checking still works generally.
      {
          .operation = ZX_HANDLE_OP_DUPLICATE,
          .handle = ep1.get(),
          .type = ZX_OBJ_TYPE_VMO,
          .rights = ZX_RIGHT_SAME_RIGHTS,
      },
      {
          .operation = ZX_HANDLE_OP_DUPLICATE,
          .handle = ep2.get(),
          .type = ZX_OBJ_TYPE_EVENTPAIR,
          .rights = ZX_RIGHT_SAME_RIGHTS,
      }};

  ASSERT_STATUS(in.write_etc(/*flags=*/0, /*bytes=*/nullptr, /*num_bytes=*/0, wr_handles.data(),
                             wr_handles.size()),
                ZX_ERR_WRONG_TYPE);
  // The write_etc should fail due to ep1, but our fake should have not had a
  // new value written to it since it was fine.
  ASSERT_STATUS(wr_handles[2].result, ZX_OK);
  ASSERT_STATUS(wr_handles[3].result, ZX_ERR_WRONG_TYPE);

  // Fix up ep1 and try again.
  wr_handles[3].type = ZX_OBJ_TYPE_EVENTPAIR;
  wr_handles[3].result = ZX_OK;
  // Testing MOVE this time.
  for (auto& handle : wr_handles) {
    handle.operation = ZX_HANDLE_OP_MOVE;
  }
  ASSERT_OK(in.write_etc(/*flags=*/0, /*bytes=*/nullptr, /*num_bytes=*/0, wr_handles.data(),
                         wr_handles.size()));
  EXPECT_OK(wr_handles[0].result);
  EXPECT_OK(wr_handles[1].result);
  EXPECT_OK(wr_handles[2].result);
  EXPECT_OK(wr_handles[3].result);
  EXPECT_OK(wr_handles[4].result);

  // Verify we fix the incoming handle types from VMO to their proper types.
  std::array<zx_handle_info_t, 5> rd_handles;
  uint32_t actual_handles;
  ASSERT_OK(out.read_etc(/*flags=*/0, /*bytes=*/nullptr, rd_handles.data(), /*num_bytes=*/0,
                         /*num_handles=*/wr_handles.size(), /*actual_bytes=*/nullptr,
                         &actual_handles));
  ASSERT_EQ(rd_handles.size(), actual_handles);
  EXPECT_EQ(ZX_OBJ_TYPE_VMO, rd_handles[0].type);
  EXPECT_EQ(ZX_OBJ_TYPE_EVENT, rd_handles[1].type);
  EXPECT_EQ(test_type, rd_handles[2].type);
  EXPECT_EQ(ZX_OBJ_TYPE_EVENTPAIR, rd_handles[3].type);
  EXPECT_EQ(ZX_OBJ_TYPE_EVENTPAIR, rd_handles[4].type);

  for (auto& handle : rd_handles) {
    EXPECT_OK(zx_handle_close(handle.handle));
  }
}

}  // namespace fake_object
