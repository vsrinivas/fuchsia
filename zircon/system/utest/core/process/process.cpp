// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <mini-process/mini-process.h>

#include <unittest/unittest.h>

namespace {

const zx_time_t kTimeoutNs = ZX_MSEC(250);

bool mini_process_sanity() {
    BEGIN_TEST;

    zx_handle_t proc;
    zx_handle_t thread;
    zx_handle_t vmar;

    ASSERT_EQ(zx_process_create(zx_job_default(), "mini-p", 3u, 0, &proc, &vmar), ZX_OK);
    ASSERT_EQ(zx_thread_create(proc, "mini-p", 2u, 0u, &thread), ZX_OK);

    zx_handle_t event;
    ASSERT_EQ(zx_event_create(0u, &event), ZX_OK);

    zx_handle_t cmd_channel;
    EXPECT_EQ(start_mini_process_etc(proc, thread, vmar, event, &cmd_channel), ZX_OK);

    EXPECT_EQ(mini_process_cmd(cmd_channel, MINIP_CMD_ECHO_MSG, nullptr), ZX_OK);

    zx_handle_t oev;
    EXPECT_EQ(mini_process_cmd(cmd_channel, MINIP_CMD_CREATE_EVENT, &oev), ZX_OK);

    EXPECT_EQ(mini_process_cmd(cmd_channel, MINIP_CMD_EXIT_NORMAL, nullptr), ZX_ERR_PEER_CLOSED);

    zx_handle_close(thread);
    zx_handle_close(proc);
    zx_handle_close(vmar);
    END_TEST;
}

bool process_start_fail() {
    BEGIN_TEST;

    zx_handle_t event1, event2;
    zx_handle_t process;
    zx_handle_t thread;

    ASSERT_EQ(zx_event_create(0u, &event1), ZX_OK);
    ASSERT_EQ(zx_event_create(0u, &event2), ZX_OK);

    ASSERT_EQ(start_mini_process(zx_job_default(), event1, &process, &thread), ZX_OK);

    zx_handle_t other_thread;
    ASSERT_EQ(zx_thread_create(process, "test", 4u, 0, &other_thread), ZX_OK);

    // Test that calling process_start() again for an existing process fails in a
    // reasonable way. Also test that the transferred object is closed.
    EXPECT_EQ(zx_process_start(process, other_thread, 0, 0, event2, 0), ZX_ERR_BAD_STATE);
    EXPECT_EQ(zx_object_signal(event2, 0u, ZX_EVENT_SIGNALED), ZX_ERR_BAD_HANDLE);

    zx_handle_close(process);
    zx_handle_close(thread);
    zx_handle_close(other_thread);
    END_TEST;
}

bool process_not_killed_via_thread_close() {
    BEGIN_TEST;

    zx_handle_t event;
    ASSERT_EQ(zx_event_create(0u, &event), ZX_OK);

    zx_handle_t process;
    zx_handle_t thread;
    ASSERT_EQ(start_mini_process(zx_job_default(), event, &process, &thread), ZX_OK);

    EXPECT_EQ(zx_handle_close(thread), ZX_OK);

    // The timeout below does not have to be large because the processing happens
    // synchronously if indeed |thread| is the last handle.
    zx_signals_t signals = 0;
    EXPECT_EQ(zx_object_wait_one(
        process, ZX_TASK_TERMINATED, zx_deadline_after(ZX_MSEC(1)), &signals), ZX_ERR_TIMED_OUT);
    EXPECT_NE(signals, ZX_TASK_TERMINATED);

    EXPECT_EQ(zx_handle_close(process), ZX_OK);
    END_TEST;
}

bool process_not_killed_via_process_close() {
    BEGIN_TEST;

    zx_handle_t event;
    ASSERT_EQ(zx_event_create(0u, &event), ZX_OK);

    zx_handle_t process;
    zx_handle_t thread;
    ASSERT_EQ(start_mini_process(zx_job_default(), event, &process, &thread), ZX_OK);

    EXPECT_EQ(zx_handle_close(process), ZX_OK);

    // The timeout below does not have to be large because the processing happens
    // synchronously if indeed |process| is the last handle.
    zx_signals_t signals;
    EXPECT_EQ(zx_object_wait_one(
        thread, ZX_TASK_TERMINATED, zx_deadline_after(ZX_MSEC(1)), &signals), ZX_ERR_TIMED_OUT);

    EXPECT_EQ(zx_handle_close(thread), ZX_OK);
    END_TEST;
}

bool kill_process_via_thread_kill() {
    BEGIN_TEST;

    zx_handle_t event;
    ASSERT_EQ(zx_event_create(0u, &event), ZX_OK);

    zx_handle_t process;
    zx_handle_t thread;
    ASSERT_EQ(start_mini_process(zx_job_default(), event, &process, &thread), ZX_OK);

    // Killing the only thread should cause the process to terminate.
    EXPECT_EQ(zx_task_kill(thread), ZX_OK);

    zx_signals_t signals;
    EXPECT_EQ(zx_object_wait_one(
        process, ZX_TASK_TERMINATED, ZX_TIME_INFINITE, &signals), ZX_OK);
    EXPECT_EQ(signals, ZX_TASK_TERMINATED);

    EXPECT_EQ(zx_handle_close(process), ZX_OK);
    EXPECT_EQ(zx_handle_close(thread), ZX_OK);
    END_TEST;
}

bool kill_process_via_vmar_destroy() {
    BEGIN_TEST;

    zx_handle_t event;
    ASSERT_EQ(zx_event_create(0u, &event), ZX_OK);

    zx_handle_t proc;
    zx_handle_t vmar;
    ASSERT_EQ(zx_process_create(zx_job_default(), "ttp", 3u, 0, &proc, &vmar), ZX_OK);

    zx_handle_t thread;
    ASSERT_EQ(zx_thread_create(proc, "th", 2u, 0u, &thread), ZX_OK);

    // Make the process busy-wait rather than using a vDSO call because
    // if it maps in the vDSO then zx_vmar_destroy is prohibited.
    EXPECT_EQ(start_mini_process_etc(proc, thread, vmar, event, nullptr),
              ZX_OK);

    // Destroying the root VMAR should cause the process to terminate.
    REGISTER_CRASH(proc);
    EXPECT_EQ(zx_vmar_destroy(vmar), ZX_OK);

    zx_signals_t signals;
    EXPECT_EQ(zx_object_wait_one(
        proc, ZX_TASK_TERMINATED, ZX_TIME_INFINITE, &signals), ZX_OK);
    signals &= ZX_TASK_TERMINATED;
    EXPECT_EQ(signals, ZX_TASK_TERMINATED);

    EXPECT_EQ(zx_handle_close(proc), ZX_OK);
    EXPECT_EQ(zx_handle_close(vmar), ZX_OK);
    EXPECT_EQ(zx_handle_close(thread), ZX_OK);
    END_TEST;
}

static zx_status_t dup_send_handle(zx_handle_t channel, zx_handle_t handle) {
    zx_handle_t dup;
    zx_status_t st = zx_handle_duplicate(handle, ZX_RIGHT_SAME_RIGHTS, &dup);
    if (st < 0)
        return st;
    return zx_channel_write(channel, 0u, nullptr, 0u, &dup, 1u);
}

bool kill_channel_handle_cycle() {
    BEGIN_TEST;

    zx_handle_t chan[2] = {ZX_HANDLE_INVALID, ZX_HANDLE_INVALID};
    ASSERT_EQ(zx_channel_create(0u, &chan[0], &chan[1]), ZX_OK);

    zx_handle_t proc1, proc2;
    zx_handle_t vmar1, vmar2;

    zx_handle_t job_child;
    ASSERT_EQ(zx_job_create(zx_job_default(), 0u, &job_child), ZX_OK);

    ASSERT_EQ(zx_process_create(job_child, "ttp1", 4u, 0u, &proc1, &vmar1), ZX_OK);
    ASSERT_EQ(zx_process_create(job_child, "ttp2", 4u, 0u, &proc2, &vmar2), ZX_OK);

    zx_handle_t thread1, thread2;

    ASSERT_EQ(zx_thread_create(proc1, "th1", 3u, 0u, &thread1), ZX_OK);
    ASSERT_EQ(zx_thread_create(proc2, "th2", 3u, 0u, &thread2), ZX_OK);

    // Now we stuff duplicated process and thread handles into each side of the channel.
    EXPECT_EQ(dup_send_handle(chan[0], proc2), ZX_OK);
    EXPECT_EQ(dup_send_handle(chan[0], thread2), ZX_OK);

    EXPECT_EQ(dup_send_handle(chan[1], proc1), ZX_OK);
    EXPECT_EQ(dup_send_handle(chan[1], thread1), ZX_OK);

    // The process start with each one side of the channel. We don't have access to the
    // channel anymore.

    zx_handle_t minip_chn[2];

    EXPECT_EQ(start_mini_process_etc(proc1, thread1, vmar1, chan[0], &minip_chn[0]),
              ZX_OK);
    EXPECT_EQ(start_mini_process_etc(proc2, thread2, vmar2, chan[1], &minip_chn[1]),
              ZX_OK);

    EXPECT_EQ(zx_handle_close(vmar2), ZX_OK);
    EXPECT_EQ(zx_handle_close(vmar1), ZX_OK);

    EXPECT_EQ(zx_handle_close(proc1), ZX_OK);
    EXPECT_EQ(zx_handle_close(proc2), ZX_OK);

    // Make (relatively) certain the processes are alive.

    zx_signals_t signals;
    EXPECT_EQ(zx_object_wait_one(
        thread1, ZX_TASK_TERMINATED, zx_deadline_after(kTimeoutNs), &signals), ZX_ERR_TIMED_OUT);

    EXPECT_EQ(zx_object_wait_one(
        thread2, ZX_TASK_TERMINATED, zx_deadline_after(kTimeoutNs), &signals), ZX_ERR_TIMED_OUT);

    // At this point the two processes have each other thread/process handles.
    EXPECT_EQ(zx_handle_close(thread1), ZX_OK);

    EXPECT_EQ(zx_object_wait_one(
        thread2, ZX_TASK_TERMINATED, zx_deadline_after(kTimeoutNs), &signals), ZX_ERR_TIMED_OUT);

    // The only way out of this situation is to use the job handle.
    EXPECT_EQ(zx_task_kill(job_child), ZX_OK);

    EXPECT_EQ(zx_object_wait_one(
        thread2, ZX_TASK_TERMINATED, ZX_TIME_INFINITE, &signals), ZX_OK);
    signals &= ZX_TASK_TERMINATED;
    EXPECT_EQ(signals, ZX_TASK_TERMINATED);

    EXPECT_EQ(zx_handle_close(thread2), ZX_OK);
    EXPECT_EQ(zx_handle_close(job_child), ZX_OK);

    END_TEST;
}

// Tests that |zx_info_process_t| fields reflect the current state of a process.
bool info_reflects_process_state() {
    BEGIN_TEST;

    // Create a process with one thread.
    zx_handle_t event;
    ASSERT_EQ(zx_event_create(0u, &event), ZX_OK);

    zx_handle_t job_child;
    ASSERT_EQ(zx_job_create(zx_job_default(), 0u, &job_child), ZX_OK);

    zx_handle_t proc;
    zx_handle_t vmar;
    ASSERT_EQ(zx_process_create(job_child, "ttp", 4u, 0u, &proc, &vmar), ZX_OK);

    zx_handle_t thread;
    ASSERT_EQ(zx_thread_create(proc, "th", 3u, 0u, &thread), ZX_OK);

    zx_info_process_t info;
    ASSERT_EQ(zx_object_get_info(
            proc, ZX_INFO_PROCESS, &info, sizeof(info), NULL, NULL), ZX_OK);
    EXPECT_FALSE(info.started, "process should not appear as started");
    EXPECT_FALSE(info.exited, "process should not appear as exited");

    zx_handle_t minip_chn;
    // Start the process and make (relatively) certain it's alive.
    ASSERT_EQ(start_mini_process_etc(proc, thread, vmar, event, &minip_chn),
              ZX_OK);
    zx_signals_t signals;
    ASSERT_EQ(zx_object_wait_one(
        proc, ZX_TASK_TERMINATED, zx_deadline_after(kTimeoutNs), &signals), ZX_ERR_TIMED_OUT);

    ASSERT_EQ(zx_object_get_info(
            proc, ZX_INFO_PROCESS, &info, sizeof(info), NULL, NULL), ZX_OK);
    EXPECT_TRUE(info.started, "process should appear as started");
    EXPECT_FALSE(info.exited, "process should not appear as exited");

    // Kill the process and wait for it to terminate.
    ASSERT_EQ(zx_task_kill(proc), ZX_OK);
    ASSERT_EQ(zx_object_wait_one(
        proc, ZX_TASK_TERMINATED, ZX_TIME_INFINITE, &signals), ZX_OK);
    ASSERT_EQ(signals, ZX_TASK_TERMINATED);

    ASSERT_EQ(zx_object_get_info(
            proc, ZX_INFO_PROCESS, &info, sizeof(info), NULL, NULL), ZX_OK);
    EXPECT_TRUE(info.started, "process should appear as started");
    EXPECT_TRUE(info.exited, "process should appear as exited");
    EXPECT_NE(info.return_code, 0, "killed process should have non-zero return code");

    END_TEST;
}

// Helper class to encapsulate starting a process with up to kNumThreads no-op child threads.
class TestProcess {
public:
    static constexpr int kMaxThreads = 3;

    // Creates the process handle, must be called first before any other function.
    bool CreateProcess() {
        BEGIN_HELPER;

        constexpr const char* kProcessName = "test_process";
        EXPECT_EQ(zx_process_create(zx_job_default(), kProcessName, strlen(kProcessName), 0,
                                    &process_, &vmar_),
                  ZX_OK);

        END_HELPER;
    }

    // Creates a child thread but does not start it.
    bool CreateThread() {
        BEGIN_HELPER;

        ASSERT_LT(num_threads_, kMaxThreads);

        zx_handle_t thread;
        char name[32];
        size_t name_length = snprintf(name, sizeof(name), "test_thread_%d", num_threads_);
        ASSERT_EQ(zx_thread_create(process_, name, name_length, 0, &thread), ZX_OK);

        threads_[num_threads_++] = thread;

        END_HELPER;
    }

    // Starts the process and all child threads.
    bool StartProcess() {
        BEGIN_HELPER;

        ASSERT_GT(num_threads_, 0);

        // The first thread must start the process.
        // We don't use this event but starting a new process requires passing it a handle.
        zx_handle_t event = ZX_HANDLE_INVALID;
        ASSERT_EQ(zx_event_create(0u, &event), ZX_OK);
        ASSERT_EQ(start_mini_process_etc(process_, threads_[0], vmar_, event, nullptr), ZX_OK);

        for (int i = 1; i < num_threads_; ++i) {
            ASSERT_EQ(start_mini_process_thread(threads_[i], vmar_), ZX_OK);
        }

        END_HELPER;
    }

    // Waits for a signal on the requested thread and returns true if the result
    // matches |expected|.
    //
    // If |expected| is ZX_ERR_TIMED_OUT this waits for a finite amount of time,
    // otherwise it waits forever.
    bool WaitForThreadSignal(int index, zx_signals_t signal, zx_status_t expected) {
        zx_time_t timeout = ZX_TIME_INFINITE;
        if (expected == ZX_ERR_TIMED_OUT)
            timeout = zx_deadline_after(kTimeoutNs);

        return zx_object_wait_one(threads_[index], signal, timeout, nullptr) == expected;
    }

    // Do this explicitly rather than in the destructor to catch any errors.
    bool StopProcess() {
        BEGIN_HELPER;

        EXPECT_EQ(zx_task_kill(process_), ZX_OK);

        EXPECT_EQ(zx_handle_close(process_), ZX_OK);
        EXPECT_EQ(zx_handle_close(vmar_), ZX_OK);
        EXPECT_EQ(zx_handle_close_many(threads_, num_threads_), ZX_OK);

        END_HELPER;
    }

    zx_handle_t process() const { return process_; }
    zx_handle_t thread(int index) const { return threads_[index]; }

private:
    zx_handle_t process_ = ZX_HANDLE_INVALID;
    zx_handle_t vmar_ = ZX_HANDLE_INVALID;

    int num_threads_ = 0;
    zx_handle_t threads_[kMaxThreads];
};

bool suspend() {
    BEGIN_TEST;

    TestProcess test_process;
    ASSERT_TRUE(test_process.CreateProcess());
    ASSERT_TRUE(test_process.CreateThread());
    ASSERT_TRUE(test_process.StartProcess());

    zx_handle_t suspend_token;
    ASSERT_EQ(zx_task_suspend(test_process.process(), &suspend_token), ZX_OK);
    ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_SUSPENDED, ZX_OK));

    ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);
    ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_OK));

    ASSERT_TRUE(test_process.StopProcess());

    END_TEST;
}

bool suspend_self() {
    BEGIN_TEST;

    zx_handle_t suspend_token;
    EXPECT_EQ(zx_task_suspend(zx_process_self(), &suspend_token), ZX_ERR_NOT_SUPPORTED);

    END_TEST;
}

bool suspend_multiple_threads() {
    BEGIN_TEST;

    TestProcess test_process;
    ASSERT_TRUE(test_process.CreateProcess());
    ASSERT_TRUE(test_process.CreateThread());
    ASSERT_TRUE(test_process.CreateThread());
    ASSERT_TRUE(test_process.CreateThread());
    ASSERT_TRUE(test_process.StartProcess());

    zx_handle_t suspend_token;
    ASSERT_EQ(zx_task_suspend(test_process.process(), &suspend_token), ZX_OK);
    ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_SUSPENDED, ZX_OK));
    ASSERT_TRUE(test_process.WaitForThreadSignal(1, ZX_THREAD_SUSPENDED, ZX_OK));
    ASSERT_TRUE(test_process.WaitForThreadSignal(2, ZX_THREAD_SUSPENDED, ZX_OK));

    ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);
    ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_OK));
    ASSERT_TRUE(test_process.WaitForThreadSignal(1, ZX_THREAD_RUNNING, ZX_OK));
    ASSERT_TRUE(test_process.WaitForThreadSignal(2, ZX_THREAD_RUNNING, ZX_OK));

    ASSERT_TRUE(test_process.StopProcess());

    END_TEST;
}

bool suspend_before_creating_threads() {
    BEGIN_TEST;

    TestProcess test_process;
    ASSERT_TRUE(test_process.CreateProcess());

    zx_handle_t suspend_token;
    ASSERT_EQ(zx_task_suspend(test_process.process(), &suspend_token), ZX_OK);

    ASSERT_TRUE(test_process.CreateThread());
    ASSERT_TRUE(test_process.StartProcess());
    ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_SUSPENDED, ZX_OK));

    ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);
    ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_OK));

    ASSERT_TRUE(test_process.StopProcess());

    END_TEST;
}

bool suspend_before_starting_threads() {
    BEGIN_TEST;

    TestProcess test_process;
    ASSERT_TRUE(test_process.CreateProcess());
    ASSERT_TRUE(test_process.CreateThread());

    zx_handle_t suspend_token;
    ASSERT_EQ(zx_task_suspend(test_process.process(), &suspend_token), ZX_OK);

    ASSERT_TRUE(test_process.StartProcess());
    ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_SUSPENDED, ZX_OK));

    ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);
    ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_OK));

    ASSERT_TRUE(test_process.StopProcess());

    END_TEST;
}

bool suspend_process_then_thread() {
    BEGIN_TEST;

    TestProcess test_process;
    ASSERT_TRUE(test_process.CreateProcess());
    ASSERT_TRUE(test_process.CreateThread());
    ASSERT_TRUE(test_process.StartProcess());

    zx_handle_t process_suspend_token;
    ASSERT_EQ(zx_task_suspend(test_process.process(), &process_suspend_token), ZX_OK);
    ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_SUSPENDED, ZX_OK));

    zx_handle_t thread_suspend_token;
    ASSERT_EQ(zx_task_suspend(test_process.thread(0), &thread_suspend_token), ZX_OK);

    // When we release the process token, the thread should remain suspended.
    ASSERT_EQ(zx_handle_close(process_suspend_token), ZX_OK);
    ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_ERR_TIMED_OUT));

    // Now close the thread token and it should resume.
    ASSERT_EQ(zx_handle_close(thread_suspend_token), ZX_OK);
    ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_OK));

    ASSERT_TRUE(test_process.StopProcess());

    END_TEST;
}

bool suspend_thread_then_process() {
    BEGIN_TEST;

    TestProcess test_process;
    ASSERT_TRUE(test_process.CreateProcess());
    ASSERT_TRUE(test_process.CreateThread());
    ASSERT_TRUE(test_process.StartProcess());

    zx_handle_t thread_suspend_token;
    ASSERT_EQ(zx_task_suspend(test_process.thread(0), &thread_suspend_token), ZX_OK);
    ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_SUSPENDED, ZX_OK));

    zx_handle_t process_suspend_token;
    ASSERT_EQ(zx_task_suspend(test_process.process(), &process_suspend_token), ZX_OK);

    ASSERT_EQ(zx_handle_close(process_suspend_token), ZX_OK);
    ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_ERR_TIMED_OUT));

    ASSERT_EQ(zx_handle_close(thread_suspend_token), ZX_OK);
    ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_OK));

    ASSERT_TRUE(test_process.StopProcess());

    END_TEST;
}

bool suspend_thread_and_process_before_starting_process() {
    BEGIN_TEST;

    TestProcess test_process;

    // Create and immediately suspend the process and thread.
    ASSERT_TRUE(test_process.CreateProcess());
    zx_handle_t process_suspend_token;
    ASSERT_EQ(zx_task_suspend(test_process.process(), &process_suspend_token), ZX_OK);

    ASSERT_TRUE(test_process.CreateThread());
    zx_handle_t thread_suspend_token;
    ASSERT_EQ(zx_task_suspend(test_process.thread(0), &thread_suspend_token), ZX_OK);

    ASSERT_TRUE(test_process.StartProcess());
    ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_SUSPENDED, ZX_OK));

    // Resume the process, thread should stay suspended.
    ASSERT_EQ(zx_handle_close(process_suspend_token), ZX_OK);
    ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_ERR_TIMED_OUT));

    ASSERT_EQ(zx_handle_close(thread_suspend_token), ZX_OK);
    ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_OK));

    ASSERT_TRUE(test_process.StopProcess());

    END_TEST;
}

bool suspend_twice() {
    BEGIN_TEST;

    TestProcess test_process;
    ASSERT_TRUE(test_process.CreateProcess());
    ASSERT_TRUE(test_process.CreateThread());
    ASSERT_TRUE(test_process.StartProcess());

    zx_handle_t suspend_tokens[2];
    ASSERT_EQ(zx_task_suspend(test_process.process(), &suspend_tokens[0]), ZX_OK);
    ASSERT_EQ(zx_task_suspend(test_process.process(), &suspend_tokens[1]), ZX_OK);
    ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_SUSPENDED, ZX_OK));

    ASSERT_EQ(zx_handle_close(suspend_tokens[0]), ZX_OK);
    ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_ERR_TIMED_OUT));

    ASSERT_EQ(zx_handle_close(suspend_tokens[1]), ZX_OK);
    ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_OK));

    ASSERT_TRUE(test_process.StopProcess());

    END_TEST;
}

bool suspend_twice_before_creating_threads() {
    BEGIN_TEST;

    TestProcess test_process;
    ASSERT_TRUE(test_process.CreateProcess());

    zx_handle_t suspend_tokens[2];
    ASSERT_EQ(zx_task_suspend(test_process.process(), &suspend_tokens[0]), ZX_OK);
    ASSERT_EQ(zx_task_suspend(test_process.process(), &suspend_tokens[1]), ZX_OK);

    ASSERT_TRUE(test_process.CreateThread());
    ASSERT_TRUE(test_process.StartProcess());
    ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_SUSPENDED, ZX_OK));

    ASSERT_EQ(zx_handle_close(suspend_tokens[0]), ZX_OK);
    ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_ERR_TIMED_OUT));

    ASSERT_EQ(zx_handle_close(suspend_tokens[1]), ZX_OK);
    ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_OK));

    ASSERT_TRUE(test_process.StopProcess());

    END_TEST;
}

// This test isn't super reliable since it has to try to suspend and resume while a thread is in
// the small window while it's dying but before it's dead, but there doesn't seem to be a way
// to deterministically hit that window so unfortunately this is the best we can do.
//
// In the expected case this test will always succeed, but if there is an underlying bug it
// will occasionally fail, so if this test begins to show flakiness it likely represents a real
// bug.
bool suspend_with_dying_thread() {
    BEGIN_TEST;

    TestProcess test_process;
    ASSERT_TRUE(test_process.CreateProcess());
    ASSERT_TRUE(test_process.CreateThread());
    ASSERT_TRUE(test_process.CreateThread());
    ASSERT_TRUE(test_process.CreateThread());
    ASSERT_TRUE(test_process.StartProcess());

    // Kill the middle thread.
    ASSERT_EQ(zx_task_kill(test_process.thread(1)), ZX_OK);

    // Now suspend the process and make sure it still works on the live threads.
    zx_handle_t suspend_token;
    ASSERT_EQ(zx_task_suspend(test_process.process(), &suspend_token), ZX_OK);
    ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_SUSPENDED, ZX_OK));
    ASSERT_TRUE(test_process.WaitForThreadSignal(2, ZX_THREAD_SUSPENDED, ZX_OK));

    ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);
    ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_OK));
    ASSERT_TRUE(test_process.WaitForThreadSignal(2, ZX_THREAD_RUNNING, ZX_OK));

    ASSERT_TRUE(test_process.StopProcess());

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(process_tests)
RUN_TEST(mini_process_sanity);
RUN_TEST(process_start_fail);
RUN_TEST(process_not_killed_via_thread_close);
RUN_TEST(process_not_killed_via_process_close);
RUN_TEST(kill_process_via_thread_kill);
RUN_TEST_ENABLE_CRASH_HANDLER(kill_process_via_vmar_destroy);
RUN_TEST(kill_channel_handle_cycle);
RUN_TEST(info_reflects_process_state);
RUN_TEST(suspend);
RUN_TEST(suspend_self);
RUN_TEST(suspend_multiple_threads);
RUN_TEST(suspend_before_creating_threads);
RUN_TEST(suspend_before_starting_threads);
RUN_TEST(suspend_process_then_thread);
RUN_TEST(suspend_thread_then_process);
RUN_TEST(suspend_thread_and_process_before_starting_process);
RUN_TEST(suspend_twice);
RUN_TEST(suspend_twice_before_creating_threads);
RUN_TEST(suspend_with_dying_thread);
END_TEST_CASE(process_tests)
