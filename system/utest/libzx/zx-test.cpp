// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include <fbl/type_support.h>
#include <lib/fzl/time.h>
#include <lib/zx/bti.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/handle.h>
#include <lib/zx/job.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/socket.h>
#include <lib/zx/suspend_token.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>
#include <lib/zx/vmar.h>
#include <unittest/unittest.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/port.h>

static zx_status_t validate_handle(zx_handle_t handle) {
    return zx_object_get_info(handle, ZX_INFO_HANDLE_VALID,
                              nullptr, 0, 0u, nullptr);
}

static bool handle_invalid_test() {
    BEGIN_TEST;
    zx::handle handle;
    // A default constructed handle is invalid.
    ASSERT_EQ(handle.release(), ZX_HANDLE_INVALID);
    END_TEST;
}

static bool handle_close_test() {
    BEGIN_TEST;
    zx_handle_t raw_event;
    ASSERT_EQ(zx_event_create(0u, &raw_event), ZX_OK);
    ASSERT_EQ(validate_handle(raw_event), ZX_OK);
    {
        zx::handle handle(raw_event);
    }
    // Make sure the handle was closed.
    ASSERT_EQ(validate_handle(raw_event), ZX_ERR_BAD_HANDLE);
    END_TEST;
}

static bool handle_move_test() {
    BEGIN_TEST;
    zx::event event;
    // Check move semantics.
    ASSERT_EQ(zx::event::create(0u, &event), ZX_OK);
    zx::handle handle(fbl::move(event));
    ASSERT_EQ(event.release(), ZX_HANDLE_INVALID);
    ASSERT_EQ(validate_handle(handle.get()), ZX_OK);
    END_TEST;
}

static bool handle_duplicate_test() {
    BEGIN_TEST;
    zx_handle_t raw_event;
    zx::handle dup;
    ASSERT_EQ(zx_event_create(0u, &raw_event), ZX_OK);
    zx::handle handle(raw_event);
    ASSERT_EQ(handle.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup), ZX_OK);
    // The duplicate must be valid as well as the original.
    ASSERT_EQ(validate_handle(dup.get()), ZX_OK);
    ASSERT_EQ(validate_handle(raw_event), ZX_OK);
    END_TEST;
}

static bool handle_replace_test() {
    BEGIN_TEST;
    zx_handle_t raw_event;
    zx::handle rep;
    ASSERT_EQ(zx_event_create(0u, &raw_event), ZX_OK);
    {
        zx::handle handle(raw_event);
        ASSERT_EQ(handle.replace(ZX_RIGHT_SAME_RIGHTS, &rep), ZX_OK);
        ASSERT_EQ(handle.release(), ZX_HANDLE_INVALID);
    }
    // The original shoould be invalid and the replacement should be valid.
    ASSERT_EQ(validate_handle(raw_event), ZX_ERR_BAD_HANDLE);
    ASSERT_EQ(validate_handle(rep.get()), ZX_OK);
    END_TEST;
}

static bool event_test() {
    BEGIN_TEST;
    zx::event event;
    ASSERT_EQ(zx::event::create(0u, &event), ZX_OK);
    ASSERT_EQ(validate_handle(event.get()), ZX_OK);
    // TODO(cpu): test more.
    END_TEST;
}

static bool event_duplicate_test() {
    BEGIN_TEST;
    zx::event event;
    zx::event dup;
    ASSERT_EQ(zx::event::create(0u, &event), ZX_OK);
    ASSERT_EQ(event.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup), ZX_OK);
    // The duplicate must be valid as well as the original.
    ASSERT_EQ(validate_handle(dup.get()), ZX_OK);
    ASSERT_EQ(validate_handle(event.get()), ZX_OK);
    END_TEST;
}

static bool bti_compilation_test() {
    BEGIN_TEST;
    zx::bti bti;
    // TODO(teisenbe): test more.
    END_TEST;
}

static bool pmt_compilation_test() {
    BEGIN_TEST;
    zx::pmt pmt;
    // TODO(teisenbe): test more.
    END_TEST;
}

static bool channel_test() {
    BEGIN_TEST;
    zx::channel channel[2];
    ASSERT_EQ(zx::channel::create(0u, &channel[0], &channel[1]), ZX_OK);
    ASSERT_EQ(validate_handle(channel[0].get()), ZX_OK);
    ASSERT_EQ(validate_handle(channel[1].get()), ZX_OK);
    // TODO(cpu): test more.
    END_TEST;
}

static bool channel_rw_test() {
    BEGIN_TEST;
    zx::eventpair eventpair[2];
    ASSERT_EQ(zx::eventpair::create(0u, &eventpair[0], &eventpair[1]), ZX_OK);

    zx::channel channel[2];
    ASSERT_EQ(zx::channel::create(0u, &channel[0], &channel[1]), ZX_OK);

    zx_handle_t handles[2] = {
        eventpair[0].release(),
        eventpair[1].release()
    };

    zx_handle_t recv[2] = {0};

    ASSERT_EQ(channel[0].write(0u, nullptr, 0u, handles, 2), ZX_OK);
    ASSERT_EQ(channel[1].read(0u, nullptr, 0u, nullptr, recv, 2, nullptr), ZX_OK);

    ASSERT_EQ(zx_handle_close(recv[0]), ZX_OK);
    ASSERT_EQ(zx_handle_close(recv[1]), ZX_OK);
    END_TEST;
}

static bool channel_rw_etc_test() {
    BEGIN_TEST;
    zx::eventpair eventpair[2];
    ASSERT_EQ(zx::eventpair::create(0u, &eventpair[0], &eventpair[1]), ZX_OK);

    zx::channel channel[2];
    ASSERT_EQ(zx::channel::create(0u, &channel[0], &channel[1]), ZX_OK);

    zx_handle_t handles[2] = {
        eventpair[0].release(),
        eventpair[1].release()
    };

    zx_handle_info_t recv[2] = {{}};
    uint32_t h_count = 0;

    ASSERT_EQ(channel[0].write(0u, nullptr, 0u, handles, 2), ZX_OK);
    ASSERT_EQ(channel[1].read_etc(0u, nullptr, 0u, nullptr, recv, 2, &h_count), ZX_OK);

    ASSERT_EQ(h_count, 2u);
    ASSERT_EQ(recv[0].type, ZX_OBJ_TYPE_EVENTPAIR, ZX_OK);
    ASSERT_EQ(recv[1].type, ZX_OBJ_TYPE_EVENTPAIR, ZX_OK);

    ASSERT_EQ(zx_handle_close(recv[0].handle), ZX_OK);
    ASSERT_EQ(zx_handle_close(recv[1].handle), ZX_OK);
    END_TEST;
}

static bool socket_test() {
    BEGIN_TEST;
    zx::socket socket[2];
    ASSERT_EQ(zx::socket::create(0u, &socket[0], &socket[1]), ZX_OK);
    ASSERT_EQ(validate_handle(socket[0].get()), ZX_OK);
    ASSERT_EQ(validate_handle(socket[1].get()), ZX_OK);
    // TODO(cpu): test more.
    END_TEST;
}

static bool eventpair_test() {
    BEGIN_TEST;
    zx::eventpair eventpair[2];
    ASSERT_EQ(zx::eventpair::create(0u, &eventpair[0], &eventpair[1]), ZX_OK);
    ASSERT_EQ(validate_handle(eventpair[0].get()), ZX_OK);
    ASSERT_EQ(validate_handle(eventpair[1].get()), ZX_OK);
    // TODO(cpu): test more.
    END_TEST;
}

static bool vmar_test() {
    BEGIN_TEST;
    zx::vmar vmar;
    const size_t size = getpagesize();
    uintptr_t addr;
    ASSERT_EQ(zx::vmar::root_self().allocate(0u, size, ZX_VM_FLAG_CAN_MAP_READ, &vmar, &addr),
              ZX_OK);
    ASSERT_EQ(validate_handle(vmar.get()), ZX_OK);
    ASSERT_EQ(vmar.destroy(), ZX_OK);
    // TODO(teisenbe): test more.
    END_TEST;
}

static bool port_test() {
    BEGIN_TEST;
    zx::port port;
    ASSERT_EQ(zx::port::create(0, &port), ZX_OK);
    ASSERT_EQ(validate_handle(port.get()), ZX_OK);

    zx::channel channel[2];
    auto key = 1111ull;
    ASSERT_EQ(zx::channel::create(0u, &channel[0], &channel[1]), ZX_OK);
    ASSERT_EQ(channel[0].wait_async(
        port, key, ZX_CHANNEL_READABLE, ZX_WAIT_ASYNC_ONCE), ZX_OK);
    ASSERT_EQ(channel[1].write(0u, "12345", 5, nullptr, 0u), ZX_OK);

    zx_port_packet_t packet = {};
    ASSERT_EQ(port.wait(zx::time(), &packet), ZX_OK);
    ASSERT_EQ(packet.key, key);
    ASSERT_EQ(packet.type, ZX_PKT_TYPE_SIGNAL_ONE);
    ASSERT_EQ(packet.signal.count, 1u);
    END_TEST;
}

static bool time_test() {
    BEGIN_TEST;

    ASSERT_EQ(zx::time().get(), 0);
    ASSERT_EQ(zx::time::infinite().get(), ZX_TIME_INFINITE);

    ASSERT_EQ(zx::duration().get(), 0);
    ASSERT_EQ(zx::duration::infinite().get(), ZX_TIME_INFINITE);

    ASSERT_EQ(zx::nsec(10).get(), ZX_NSEC(10));
    ASSERT_EQ(zx::nsec(10).to_nsecs(), 10);
    ASSERT_EQ(zx::usec(10).get(), ZX_USEC(10));
    ASSERT_EQ(zx::usec(10).to_usecs(), 10);
    ASSERT_EQ(zx::msec(10).get(), ZX_MSEC(10));
    ASSERT_EQ(zx::msec(10).to_msecs(), 10);
    ASSERT_EQ(zx::sec(10).get(), ZX_SEC(10));
    ASSERT_EQ(zx::sec(10).to_secs(), 10);
    ASSERT_EQ(zx::min(10).get(), ZX_MIN(10));
    ASSERT_EQ(zx::min(10).to_mins(), 10);
    ASSERT_EQ(zx::hour(10).get(), ZX_HOUR(10));
    ASSERT_EQ(zx::hour(10).to_hours(), 10);

    ASSERT_EQ((zx::time() + zx::usec(19)).get(), ZX_USEC(19));
    ASSERT_EQ((zx::time::infinite() - zx::time()).get(), ZX_TIME_INFINITE);
    ASSERT_EQ((zx::time::infinite() - zx::time::infinite()).get(), 0);
    ASSERT_EQ((zx::time() + zx::duration::infinite()).get(), ZX_TIME_INFINITE);

    zx::duration d(0u);
    d += zx::nsec(19);
    ASSERT_EQ(d.get(), ZX_NSEC(19));
    d -= zx::nsec(19);
    ASSERT_EQ(d.get(), ZX_NSEC(0));

    d = zx::min(1);
    d *= 19u;
    ASSERT_EQ(d.get(), ZX_MIN(19));
    d /= 19u;
    ASSERT_EQ(d.get(), ZX_MIN(1));

    ASSERT_EQ((zx::sec(19) % zx::sec(7)).get(), ZX_SEC(5));

    zx::time t(0u);
    t += zx::msec(19);
    ASSERT_EQ(t.get(), ZX_MSEC(19));
    t -= zx::msec(19);
    ASSERT_EQ(t.get(), ZX_MSEC(0));

    // Just a smoke test
    ASSERT_GE(zx::deadline_after(zx::usec(10)).get(), ZX_USEC(10));

    END_TEST;
}

static bool ticks_test() {
    BEGIN_TEST;

    ASSERT_EQ(zx::ticks().get(), 0);

    zx::ticks before = zx::ticks::now();
    ASSERT_GT(before.get(), 0);
    zx::ticks after = before + zx::ticks(1);

    ASSERT_LT(before.get(), after.get());
    ASSERT_TRUE(before < after);
    after -= zx::ticks(1);
    ASSERT_EQ(before.get(), after.get());
    ASSERT_TRUE(before == after);

    ASSERT_EQ(zx::ticks::per_second().get(), zx_ticks_per_second());

    // Compare a duration (nanoseconds) with the ticks equivalent.
    zx::ticks second = zx::ticks::per_second();
    ASSERT_EQ(fzl::TicksToNs(second).get(), zx::sec(1).get());
    ASSERT_TRUE(fzl::TicksToNs(second) == zx::sec(1));

    // Hopefully, we haven't moved backwards in time.
    after = zx::ticks::now();
    ASSERT_LE(before.get(), after.get());
    ASSERT_TRUE(before <= after);

    END_TEST;
}

template <typename T>
static bool reference_thing(const T& p) {
    BEGIN_HELPER;
    ASSERT_TRUE(static_cast<bool>(p), "invalid handle");
    END_HELPER;
}

static bool thread_self_test() {
    BEGIN_TEST;

    zx_handle_t raw = zx_thread_self();
    ASSERT_EQ(validate_handle(raw), ZX_OK);

    EXPECT_TRUE(reference_thing<zx::thread>(zx::thread::self()));
    EXPECT_EQ(validate_handle(raw), ZX_OK);

    // This does not compile:
    //const zx::thread self = zx::thread::self();

    END_TEST;
}

static void thread_suspend_test_fn(uintptr_t, uintptr_t) {
    zx_nanosleep(zx_deadline_after(ZX_SEC(1000)));
    zx_thread_exit();
}

static bool thread_suspend_test() {
    BEGIN_TEST;

    zx::thread thread;
    ASSERT_EQ(zx::thread::create(zx::process::self(), "test", 4, 0, &thread), ZX_OK);

    // Make a little stack and start the thread. Note: stack grows down so pass the high address.
    alignas(16) static uint8_t stack_storage[64];
    uint8_t* stack = stack_storage + sizeof(stack_storage);
    ASSERT_EQ(thread.start(&thread_suspend_test_fn, stack, 0, 0), ZX_OK);

    zx::suspend_token suspend;
    EXPECT_EQ(thread.suspend(&suspend), ZX_OK);
    EXPECT_TRUE(suspend);

    suspend.reset();
    EXPECT_EQ(thread.kill(), ZX_OK);

    END_TEST;
}

static bool process_self_test() {
    BEGIN_TEST;

    zx_handle_t raw = zx_process_self();
    ASSERT_EQ(validate_handle(raw), ZX_OK);

    EXPECT_TRUE(reference_thing<zx::process>(zx::process::self()));
    EXPECT_EQ(validate_handle(raw), ZX_OK);

    // This does not compile:
    //const zx::process self = zx::process::self();

    END_TEST;
}

static bool vmar_root_self_test() {
    BEGIN_TEST;

    zx_handle_t raw = zx_vmar_root_self();
    ASSERT_EQ(validate_handle(raw), ZX_OK);

    EXPECT_TRUE(reference_thing<zx::vmar>(zx::vmar::root_self()));
    EXPECT_EQ(validate_handle(raw), ZX_OK);

    // This does not compile:
    //const zx::vmar root_self = zx::vmar::root_self();

    END_TEST;
}

static bool job_default_test() {
    BEGIN_TEST;

    zx_handle_t raw = zx_job_default();
    ASSERT_EQ(validate_handle(raw), ZX_OK);

    EXPECT_TRUE(reference_thing<zx::job>(*zx::job::default_job()));
    EXPECT_EQ(validate_handle(raw), ZX_OK);

    // This does not compile:
    //const zx::job default_job = zx::job::default_job();

    END_TEST;
}

static bool takes_any_handle(const zx::handle& handle) {
    return handle.is_valid();
}

static bool handle_conversion_test() {
    BEGIN_TEST;
    EXPECT_TRUE(takes_any_handle(zx::unowned_handle::wrap(zx_thread_self())));
    ASSERT_EQ(validate_handle(zx_thread_self()), ZX_OK);
    END_TEST;
}

static bool unowned_test() {
    BEGIN_TEST;

    // Create a handle to test with.
    zx::event handle;
    ASSERT_EQ(zx::event::create(0, &handle), ZX_OK);
    ASSERT_EQ(validate_handle(handle.get()), ZX_OK);

    // Verify that unowned<T>(zx_handle_t) doesn't close handle on teardown.
    {
      zx::unowned<zx::event> unowned(handle.get());
      EXPECT_EQ(unowned->get(), handle.get());
      EXPECT_TRUE(reference_thing<zx::event>(*unowned));
    }
    ASSERT_EQ(validate_handle(handle.get()), ZX_OK);

    // Verify that unowned<T>(const T&) doesn't close handle on teardown.
    {
      zx::unowned<zx::event> unowned(handle);
      EXPECT_EQ(unowned->get(), handle.get());
      EXPECT_TRUE(reference_thing<zx::event>(*unowned));
    }
    ASSERT_EQ(validate_handle(handle.get()), ZX_OK);

    // Verify that unowned<T>(const unowned<T>&) doesn't close on teardown.
    {
      zx::unowned<zx::event> unowned(handle);
      EXPECT_TRUE(reference_thing<zx::event>(*unowned));

      zx::unowned<zx::event> unowned2(unowned);
      EXPECT_EQ(unowned->get(), unowned2->get());
      EXPECT_TRUE(reference_thing<zx::event>(*unowned2));
      EXPECT_TRUE(reference_thing<zx::event>(*unowned));
    }
    ASSERT_EQ(validate_handle(handle.get()), ZX_OK);

    // Verify copy-assignment from unowned<> to unowned<> doesn't close.
    {
      zx::unowned<zx::event> unowned(handle);
      EXPECT_TRUE(reference_thing<zx::event>(*unowned));

      zx::unowned<zx::event> unowned2;
      ASSERT_FALSE(unowned2->is_valid());

      const zx::unowned<zx::event>& assign_ref = unowned2 = unowned;
      EXPECT_EQ(assign_ref->get(), unowned2->get());
      EXPECT_EQ(unowned->get(), unowned2->get());
      EXPECT_TRUE(reference_thing<zx::event>(*unowned2));
      EXPECT_TRUE(reference_thing<zx::event>(*unowned));
    }
    ASSERT_EQ(validate_handle(handle.get()), ZX_OK);

    // Verify move from unowned<> to unowned<> doesn't close on teardown.
    {
      zx::unowned<zx::event> unowned(handle);
      EXPECT_TRUE(reference_thing<zx::event>(*unowned));

      zx::unowned<zx::event> unowned2(
          static_cast<zx::unowned<zx::event>&&>(unowned));
      EXPECT_EQ(unowned2->get(), handle.get());
      EXPECT_TRUE(reference_thing<zx::event>(*unowned2));
      EXPECT_FALSE(unowned->is_valid());
    }
    ASSERT_EQ(validate_handle(handle.get()), ZX_OK);

    // Verify move-assignment from unowned<> to unowned<> doesn't close.
    {
      zx::unowned<zx::event> unowned(handle);
      EXPECT_TRUE(reference_thing<zx::event>(*unowned));

      zx::unowned<zx::event> unowned2;
      ASSERT_FALSE(unowned2->is_valid());

      const zx::unowned<zx::event>& assign_ref =
          unowned2 = static_cast<zx::unowned<zx::event>&&>(unowned);
      EXPECT_EQ(assign_ref->get(), unowned2->get());
      EXPECT_TRUE(reference_thing<zx::event>(*unowned2));
      EXPECT_FALSE(unowned->is_valid());
    }
    ASSERT_EQ(validate_handle(handle.get()), ZX_OK);

    // Verify move-assignment into non-empty unowned<>  doesn't close.
    {
      zx::unowned<zx::event> unowned(handle);
      EXPECT_TRUE(reference_thing<zx::event>(*unowned));

      zx::unowned<zx::event> unowned2(handle);
      EXPECT_TRUE(reference_thing<zx::event>(*unowned2));

      unowned2 = static_cast<zx::unowned<zx::event>&&>(unowned);
      EXPECT_EQ(unowned2->get(), handle.get());
      EXPECT_TRUE(reference_thing<zx::event>(*unowned2));
      EXPECT_FALSE(unowned->is_valid());
    }
    ASSERT_EQ(validate_handle(handle.get()), ZX_OK);

    // Explicitly verify dereference operator allows methods to be called.
    {
      zx::unowned<zx::event> unowned(handle);
      EXPECT_TRUE(reference_thing<zx::event>(*unowned));

      const zx::event& event_ref = *unowned;
      zx::event duplicate;
      EXPECT_EQ(event_ref.duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate), ZX_OK);
    }
    ASSERT_EQ(validate_handle(handle.get()), ZX_OK);

    // Explicitly verify member access operator allows methods to be called.
    {
      zx::unowned<zx::event> unowned(handle);
      EXPECT_TRUE(reference_thing<zx::event>(*unowned));

      zx::event duplicate;
      EXPECT_EQ(unowned->duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate), ZX_OK);
    }
    ASSERT_EQ(validate_handle(handle.get()), ZX_OK);

    END_TEST;
}

static bool get_child_test() {
    BEGIN_TEST;

    {
        // Verify handle and job overrides of get_child() can find this process
        // by KOID.
        zx_info_handle_basic_t info = {};
        ASSERT_EQ(zx_object_get_info(zx_process_self(), ZX_INFO_HANDLE_BASIC,
                                     &info, sizeof(info), nullptr, nullptr),
              ZX_OK);

        zx::handle as_handle;
        ASSERT_EQ(zx::job::default_job()->get_child(
                      info.koid, ZX_RIGHT_SAME_RIGHTS, &as_handle), ZX_OK);
        ASSERT_EQ(validate_handle(as_handle.get()), ZX_OK);

        zx::process as_process;
        ASSERT_EQ(zx::job::default_job()->get_child(
                      info.koid, ZX_RIGHT_SAME_RIGHTS, &as_process), ZX_OK);
        ASSERT_EQ(validate_handle(as_process.get()), ZX_OK);
    }

    {
        // Verify handle and thread overrides of get_child() can find this
        // thread by KOID.
        zx_info_handle_basic_t info = {};
        ASSERT_EQ(zx_object_get_info(zx_thread_self(), ZX_INFO_HANDLE_BASIC,
                                     &info, sizeof(info), nullptr, nullptr),
                  ZX_OK);

        zx::handle as_handle;
        ASSERT_EQ(zx::process::self().get_child(
                      info.koid, ZX_RIGHT_SAME_RIGHTS, &as_handle), ZX_OK);
        ASSERT_EQ(validate_handle(as_handle.get()), ZX_OK);

        zx::thread as_thread;
        ASSERT_EQ(zx::process::self().get_child(
                      info.koid, ZX_RIGHT_SAME_RIGHTS, &as_thread), ZX_OK);
        ASSERT_EQ(validate_handle(as_thread.get()), ZX_OK);
    }

    END_TEST;
}

BEGIN_TEST_CASE(libzx_tests)
RUN_TEST(handle_invalid_test)
RUN_TEST(handle_close_test)
RUN_TEST(handle_move_test)
RUN_TEST(handle_duplicate_test)
RUN_TEST(handle_replace_test)
RUN_TEST(event_test)
RUN_TEST(event_duplicate_test)
RUN_TEST(bti_compilation_test)
RUN_TEST(pmt_compilation_test)
RUN_TEST(channel_test)
RUN_TEST(channel_rw_test)
RUN_TEST(channel_rw_etc_test)
RUN_TEST(socket_test)
RUN_TEST(eventpair_test)
RUN_TEST(vmar_test)
RUN_TEST(port_test)
RUN_TEST(time_test)
RUN_TEST(ticks_test)
RUN_TEST(thread_self_test)
RUN_TEST(thread_suspend_test)
RUN_TEST(process_self_test)
RUN_TEST(vmar_root_self_test)
RUN_TEST(job_default_test)
RUN_TEST(unowned_test)
RUN_TEST(get_child_test)
END_TEST_CASE(libzx_tests)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
