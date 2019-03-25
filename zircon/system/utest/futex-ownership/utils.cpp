// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>
#include <lib/fdio/spawn.h>
#include <lib/zx/process.h>
#include <unittest/unittest.h>
#include <zircon/processargs.h>
#include <zircon/process.h>

#include "utils.h"

const char* ExternalThread::program_name_ = nullptr;
const char* ExternalThread::helper_flag_ = "futex-owner-helper";

bool WaitFor(zx_duration_t timeout, WaitFn wait_fn) {
    constexpr zx_duration_t WAIT_POLL_INTERVAL = ZX_MSEC(1);

    ZX_ASSERT((timeout >= 0) && (timeout <= ZX_SEC(10)));
    zx_time_t deadline = zx_deadline_after(timeout);

    while (!wait_fn()) {
        if (zx_clock_get(ZX_CLOCK_MONOTONIC) > deadline) {
            return false;
        }
        zx_nanosleep(WAIT_POLL_INTERVAL);
    }

    return true;
}

zx_koid_t CurrentThreadKoid() {
    zx_info_handle_basic info;
    zx_status_t res = zx_object_get_info(zx_thread_self(), ZX_INFO_HANDLE_BASIC,
                                         &info, sizeof(info), nullptr, nullptr);
    ZX_ASSERT(res == ZX_OK);
    return info.koid;
}

zx_status_t Event::Wait(zx_duration_t timeout) {
    zx_time_t deadline = (timeout == ZX_TIME_INFINITE)
                       ? ZX_TIME_INFINITE
                       : zx_deadline_after(timeout);

    while (signaled_.load(fbl::memory_order_relaxed) == 0) {
        zx_status_t res = zx_futex_wait(&signaled_, 0, ZX_HANDLE_INVALID, deadline);
        if ((res != ZX_OK) && (res != ZX_ERR_BAD_STATE)) {
            return res;
        }
    }

    return ZX_OK;
}

void Event::Signal() {
    if (signaled_.load(fbl::memory_order_relaxed) == 0) {
        signaled_.store(1, fbl::memory_order_relaxed);
        zx_futex_wake(&signaled_, UINT32_MAX);
    }
}

void Event::Reset() {
    signaled_.store(0, fbl::memory_order_relaxed);
}

void Thread::Reset() {
    handle_.reset();
    koid_ = ZX_KOID_INVALID;
    SetState(State::WAITING_TO_START);
    thunk_ = nullptr;
    started_evt_.Reset();
    stop_evt_.Reset();
}

bool Thread::Start(const char* name, Thunk thunk) {
    BEGIN_HELPER;
    ZX_ASSERT(static_cast<bool>(thunk_) == false);

    thunk_ = std::move(thunk);

    auto internal_thunk = [](void* ctx) -> int {
        auto t = reinterpret_cast<Thread*>(ctx);

        // Create a clone of the zx_thread_self handle.  This handle is owned by
        // the runtime, not owned by us.  The runtime will automatically close
        // this handle when the thread exits, invalidating it in the process.
        // If we want to be able to do things like test to see if a thread state
        // has reached DEAD, we need to make our own handle to hold onto.  Do so
        // now.
        //
        // Success or fail, make sure we flag ourselves as started before moving
        // on.  We don't want to hold up the test framework.  They will discover
        // that we failed to start when they check our handle and discover that
        // it failed to duplicate.
        zx::unowned_thread thread_self(zx_thread_self());
        int ret = static_cast<int>(thread_self->duplicate(ZX_RIGHT_SAME_RIGHTS, &(t->handle_)));
        t->koid_ = CurrentThreadKoid();
        t->started_evt_.Signal();

        if (ret == static_cast<int>(ZX_OK)) {
            t->SetState(State::RUNNING);
            ret = t->thunk_();
        }

        t->SetState(State::WAITING_TO_STOP);
        t->stop_evt_.Wait(ZX_TIME_INFINITE);
        t->SetState(State::STOPPED);
        return ret;
    };

    int res = thrd_create_with_name(&thread_, internal_thunk, this, name);
    ASSERT_EQ(res, 0);
    ASSERT_EQ(started_evt_.Wait(THREAD_TIMEOUT), ZX_OK);
    ASSERT_TRUE(handle_.is_valid());
    ASSERT_NE(koid_, ZX_KOID_INVALID);

    END_HELPER;
}

zx_status_t Thread::Stop() {
    if (!handle_.is_valid()) {
        return ZX_ERR_BAD_STATE;
    }

    stop_evt_.Signal();

    zx_time_t deadline = zx_deadline_after(THREAD_TIMEOUT);
    while (state() != State::STOPPED) {
        if (zx_clock_get(ZX_CLOCK_MONOTONIC) > deadline) {
            return ZX_ERR_TIMED_OUT;
        }
        zx_nanosleep(THREAD_POLL_INTERVAL);
    }

    thrd_join(thread_, nullptr);
    Reset();

    return ZX_OK;
}

zx_status_t Thread::GetRunState(uint32_t* run_state) const {
    ZX_DEBUG_ASSERT(run_state != nullptr);
    if (!handle_.is_valid()) {
        return ZX_ERR_BAD_STATE;
    }

    zx_info_thread_t info;
    zx_status_t res = handle_.get_info(ZX_INFO_THREAD, &info, sizeof(info), nullptr, nullptr);
    if (res != ZX_OK) {
        return res;
    }

    *run_state = info.state;
    return ZX_OK;
}

int ExternalThread::DoHelperThread() {
    // Get our channel to our parent from our environment.
    zx::channel remote(zx_take_startup_handle(PA_HND(PA_USER0, 0)));
    if (!remote.is_valid()) { return -__LINE__; }

    // Duplicate our thread handle.
    zx::unowned_thread cur_thread(zx_thread_self());
    zx::thread thread_copy;
    if (cur_thread->duplicate(ZX_RIGHT_SAME_RIGHTS, &thread_copy) != ZX_OK) {
        return -__LINE__;
    }

    // Send a copy of our thread handle back to our our parent.
    if (zx_handle_t leaked = thread_copy.release();
        remote.write(0, nullptr, 0, &leaked, 1) != ZX_OK) {
        return -__LINE__;
    }

    // Block until our parent closes our control channel, then exit.  Do not
    // block forever... If the worst happens, we don't want to be leaking
    // processes in our test environment.  For now, waiting 2 minutes seems like
    // a Very Long Time to wait for our parent to give us the all clear.
    constexpr zx::duration TIMEOUT(ZX_SEC(120));
    zx_status_t wait_res;
    wait_res = remote.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::deadline_after(TIMEOUT), nullptr);

    return (wait_res == ZX_OK) ? 0 : -__LINE__;
}

bool ExternalThread::Start() {
    BEGIN_HELPER;

    auto on_failure = fbl::MakeAutoCall([this]() { Stop(); });

    // Make sure that we have a program name and have not already started.
    ASSERT_NONNULL(program_name_);
    ASSERT_EQ(external_thread_.get(), ZX_HANDLE_INVALID);
    ASSERT_EQ(control_channel_.get(), ZX_HANDLE_INVALID);

    // Create the channel we will use for talking to our external thread.
    zx::channel local, remote;
    ASSERT_EQ(zx::channel::create(0, &local, &remote), ZX_OK);

    const char* args[] = { program_name_, helper_flag_, nullptr };
    struct fdio_spawn_action transfer_channel_action = {
        .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
        .h = { .id = PA_HND(PA_USER0, 0), .handle = remote.release() }
    };

    zx::process proc;
    char err_msg_out[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
    zx_status_t res = fdio_spawn_etc(ZX_HANDLE_INVALID,
                                     FDIO_SPAWN_CLONE_ALL,
                                     program_name_,
                                     args,
                                     nullptr,
                                     1,
                                     &transfer_channel_action,
                                     proc.reset_and_get_address(),
                                     err_msg_out);
    ASSERT_EQ(res, ZX_OK, err_msg_out);

    // Get our child's thread handle, but do not wait forever.
    constexpr zx::duration TIMEOUT(ZX_MSEC(2500));
    constexpr zx_signals_t WAKE_SIGS = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
    zx_signals_t sigs = 0;
    ASSERT_EQ(local.wait_one(WAKE_SIGS, zx::deadline_after(TIMEOUT), &sigs), ZX_OK);
    ASSERT_NE(sigs & ZX_CHANNEL_READABLE, 0);

    uint32_t rxed_handles = 0;
    ASSERT_EQ(local.read(0, nullptr, 0, nullptr,
                         external_thread_.reset_and_get_address(), 1, &rxed_handles), ZX_OK);
    ASSERT_EQ(rxed_handles, 1u);

    // Things went well!  Cancel our on_failure cleanup routine and Stash our
    // control channel endpoint, we will close it when it is time for our
    // external thread and process to terminate.
    control_channel_ = std::move(local);
    on_failure.cancel();
    END_HELPER;
}

void ExternalThread::Stop() {
    external_thread_.reset();
    control_channel_.reset();
}
