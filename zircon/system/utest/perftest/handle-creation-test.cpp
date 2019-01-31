// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/fifo.h>
#include <lib/zx/job.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <lib/zx/vmar.h>
#include <perftest/perftest.h>

namespace {

// These tests measure the times taken to create and close various types of
// Zircon handle.  Strictly speaking, they test creating Zircon objects as
// well as creating handles.
//
// In each test, closing the handles is done implicitly by destructors.

bool ChannelCreateTest(perftest::RepeatState* state) {
    state->DeclareStep("create");
    state->DeclareStep("close");
    while (state->KeepRunning()) {
        zx::channel handle1;
        zx::channel handle2;
        ZX_ASSERT(zx::channel::create(0, &handle1, &handle2) == ZX_OK);
        state->NextStep();
    }
    return true;
}

bool EventCreateTest(perftest::RepeatState* state) {
    state->DeclareStep("create");
    state->DeclareStep("close");
    while (state->KeepRunning()) {
        zx::event handle;
        ZX_ASSERT(zx::event::create(0, &handle) == ZX_OK);
        state->NextStep();
    }
    return true;
}

bool EventPairCreateTest(perftest::RepeatState* state) {
    state->DeclareStep("create");
    state->DeclareStep("close");
    while (state->KeepRunning()) {
        zx::eventpair handle1;
        zx::eventpair handle2;
        ZX_ASSERT(zx::eventpair::create(0, &handle1, &handle2) == ZX_OK);
        state->NextStep();
    }
    return true;
}

bool FifoCreateTest(perftest::RepeatState* state) {
    state->DeclareStep("create");
    state->DeclareStep("close");
    while (state->KeepRunning()) {
        zx::fifo handle1;
        zx::fifo handle2;
        const uint32_t kElementCount = 2;
        const uint32_t kElementSize = 2048;
        ZX_ASSERT(zx::fifo::create(kElementCount, kElementSize, 0, &handle1,
                                   &handle2) == ZX_OK);
        state->NextStep();
    }
    return true;
}

bool PortCreateTest(perftest::RepeatState* state) {
    state->DeclareStep("create");
    state->DeclareStep("close");
    while (state->KeepRunning()) {
        zx::port handle;
        ZX_ASSERT(zx::port::create(0, &handle) == ZX_OK);
        state->NextStep();
    }
    return true;
}

// Note that this only creates a Zircon process object.  It does not start
// the process.
bool ProcessCreateTest(perftest::RepeatState* state) {
    state->DeclareStep("create");
    state->DeclareStep("close");
    while (state->KeepRunning()) {
        zx::process process;
        zx::vmar root_vmar;
        static const char kName[] = "perftest-process";
        ZX_ASSERT(zx::process::create(
                      *zx::job::default_job(), kName, sizeof(kName) - 1, 0,
                      &process, &root_vmar) == ZX_OK);
        state->NextStep();
    }
    return true;
}

// Note that this only creates a Zircon thread object.  It does not start
// the thread.
bool ThreadCreateTest(perftest::RepeatState* state) {
    state->DeclareStep("create");
    state->DeclareStep("close");
    while (state->KeepRunning()) {
        zx::thread handle;
        static const char kName[] = "perftest-process";
        ZX_ASSERT(zx::thread::create(*zx::process::self(), kName,
                                     sizeof(kName) - 1, 0, &handle) == ZX_OK);
        state->NextStep();
    }
    return true;
}

bool VmoCreateTest(perftest::RepeatState* state) {
    state->DeclareStep("create");
    state->DeclareStep("close");
    while (state->KeepRunning()) {
        zx::vmo handle;
        const size_t kSizeInBytes = 64 * 1024;
        ZX_ASSERT(zx::vmo::create(kSizeInBytes, 0, &handle) == ZX_OK);
        state->NextStep();
    }
    return true;
}

void RegisterTests() {
    perftest::RegisterTest("HandleCreate_Channel", ChannelCreateTest);
    perftest::RegisterTest("HandleCreate_Event", EventCreateTest);
    perftest::RegisterTest("HandleCreate_EventPair", EventPairCreateTest);
    perftest::RegisterTest("HandleCreate_Fifo", FifoCreateTest);
    perftest::RegisterTest("HandleCreate_Port", PortCreateTest);
    perftest::RegisterTest("HandleCreate_Process", ProcessCreateTest);
    perftest::RegisterTest("HandleCreate_Thread", ThreadCreateTest);
    perftest::RegisterTest("HandleCreate_Vmo", VmoCreateTest);
}
PERFTEST_CTOR(RegisterTests);

}  // namespace
