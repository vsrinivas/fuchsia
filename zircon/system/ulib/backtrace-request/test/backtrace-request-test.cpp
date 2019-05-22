// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/backtrace-request/backtrace-request.h>

#include <condition_variable>
#include <mutex>
#include <thread>

#include <lib/backtrace-request/backtrace-request-utils.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/exception.h>
#include <lib/zx/thread.h>
#include <zxtest/zxtest.h>

namespace {

TEST(BacktraceRequest, RequestAndResume) {
    constexpr zx_signals_t kChannelReadySignal = ZX_USER_SIGNAL_0;
    constexpr zx_signals_t kBacktraceReturnedSignal = ZX_USER_SIGNAL_1;
    zx::event event;
    ASSERT_OK(zx::event::create(0, &event));

    zx::channel exception_channel;
    std::thread thread([&] {
        // Attach an exception handler so we can resume the request thread
        // locally without going to up the system crash service.
        ASSERT_OK(zx::thread::self()->create_exception_channel(0, &exception_channel));
        ASSERT_OK(event.signal(0, kChannelReadySignal));

        // Request the backtrace, then once it returns flip the signal to prove
        // we got control back at the right place.
        backtrace_request();
        ASSERT_OK(event.signal(0, kBacktraceReturnedSignal));
    });

    ASSERT_OK(event.wait_one(kChannelReadySignal, zx::time::infinite(), nullptr));

    // Pull out the exception and all the state we need.
    zx_exception_info_t info;
    zx::exception exception;
    zx::thread exception_thread;
    zx_thread_state_general_regs_t regs;
    ASSERT_OK(exception_channel.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), nullptr));
    ASSERT_OK(exception_channel.read(0, &info, exception.reset_and_get_address(), sizeof(info), 1,
                                     nullptr, nullptr));
    ASSERT_OK(exception.get_thread(&exception_thread));
    ASSERT_OK(exception_thread.read_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)));

    // Make sure this is a backtrace and clean it up.
    ASSERT_TRUE(is_backtrace_request(info.type, &regs));
    ASSERT_OK(cleanup_backtrace_request(exception_thread.get(), &regs));

    // Resume the thread, it should pick up where it left off.
    uint32_t handled = ZX_EXCEPTION_STATE_HANDLED;
    ASSERT_OK(exception.set_property(ZX_PROP_EXCEPTION_STATE, &handled, sizeof(handled)));
    exception.reset();

    ASSERT_OK(event.wait_one(kBacktraceReturnedSignal, zx::time::infinite(), nullptr));
    thread.join();
}

void DoSegfault(uintptr_t, uintptr_t) {
    volatile int* p = 0;
    *p = 0;
}

TEST(BacktraceRequest, IgnoreNormalException) {
    // Can't use std::thread here because we want to zx_task_kill() it so the
    // exception doesn't bubble up to the system crash handler.
    zx::thread thread;
    ASSERT_OK(zx::thread::create(*zx::process::self(), "bt-req", strlen("bt-req"), 0, &thread));

    zx::channel exception_channel;
    ASSERT_OK(thread.create_exception_channel(0, &exception_channel));

    alignas(16) static uint8_t stack[64];
    ASSERT_OK(thread.start(&DoSegfault, stack + sizeof(stack), 0, 0));

    zx_exception_info_t info;
    zx::exception exception;
    zx::thread exception_thread;
    zx_thread_state_general_regs_t regs;
    ASSERT_OK(exception_channel.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), nullptr));
    ASSERT_OK(exception_channel.read(0, &info, exception.reset_and_get_address(), sizeof(info), 1,
                                     nullptr, nullptr));
    ASSERT_OK(exception.get_thread(&exception_thread));
    ASSERT_OK(exception_thread.read_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)));

    ASSERT_FALSE(is_backtrace_request(info.type, &regs));

    ASSERT_OK(thread.kill());
    ASSERT_OK(thread.wait_one(ZX_THREAD_TERMINATED, zx::time::infinite(), nullptr));
}

} // namespace
