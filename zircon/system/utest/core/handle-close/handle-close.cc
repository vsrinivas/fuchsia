// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <fbl/vector.h>
#include <lib/zx/eventpair.h>
#include <zxtest/zxtest.h>

namespace {

constexpr uint32_t kNumEventpairCombos = 4u;
constexpr uint32_t kNumEventpairsInvalid = 2u;
constexpr uint32_t kOptions = 0u;

void PeerWasClosed(const zx::eventpair& eventpair) {
        zx_signals_t signals;
        ASSERT_OK(eventpair.wait_one(ZX_EVENTPAIR_PEER_CLOSED, zx::time(),
                  &signals));
        ASSERT_EQ(signals & ZX_EVENTPAIR_PEER_CLOSED, ZX_EVENTPAIR_PEER_CLOSED);
}

TEST(HandleCloseTest, Many) {
    zx::eventpair eventpair_0[kNumEventpairCombos];
    zx::eventpair eventpair_1[kNumEventpairCombos];
    zx_handle_t handles[kNumEventpairCombos] = {};

    for (size_t idx = 0u; idx < kNumEventpairCombos; ++idx) {
        ASSERT_OK(zx::eventpair::create(kOptions, &eventpair_0[idx],
                  &eventpair_1[idx]));
        // We don't transfer ownership, just in case close many fails, and we can try
        // closing each handle individually when the test scope exits.
        handles[idx] = eventpair_0[idx].get();
    }
    // Close all of the handles from eventpair_0.
    ASSERT_OK(zx_handle_close_many(handles, kNumEventpairCombos));

    // Verify all the peers of the eventpair were indeed closed.
    for (const auto& eventpair : eventpair_1) {
            ASSERT_NO_FATAL_FAILURES(PeerWasClosed(eventpair));
    }
}

TEST(HandleCloseTest, ManyInvalidHandlesShouldNotFail) {
    // The handles layout: 0 1 2 3 : invalid invalid : 0 1 2 3
    zx::eventpair eventpair_0[kNumEventpairCombos];
    zx::eventpair eventpair_1[kNumEventpairCombos];
    zx_handle_t handles[kNumEventpairCombos + kNumEventpairsInvalid] = {};

    for (size_t idx = 0u; idx < kNumEventpairCombos; ++idx) {
        ASSERT_OK(zx::eventpair::create(kOptions, &eventpair_0[idx],
                  &eventpair_1[idx]));
        // We don't transfer ownership, just in case close many fails, and we can try
        // closing each handle individually when the test scope exits.
        handles[idx] = eventpair_0[idx].get();
    }

    // This invokes close_many with the first 4 valid handles, plus the
    // next two invalid handles, and should close all without failure.
    ASSERT_OK(zx_handle_close_many(handles, kNumEventpairCombos +
              kNumEventpairsInvalid));

    // Verify all the peers of the eventpair were indeed closed.
    for (const auto& eventpair : eventpair_1) {
            ASSERT_NO_FATAL_FAILURES(PeerWasClosed(eventpair));
    }
}

TEST(HandleCloseTest, ManyDuplicateTest) {
    // The handles layout: 0 1 0 1 2 3 : 0 1 2 3
    zx::eventpair eventpair_0[kNumEventpairCombos];
    zx::eventpair eventpair_1[kNumEventpairCombos];
    zx_handle_t handles[kNumEventpairCombos + kNumEventpairsInvalid] = {};

    for (size_t idx = 0u; idx < kNumEventpairCombos; ++idx) {
        ASSERT_OK(zx::eventpair::create(kOptions, &eventpair_0[idx],
                  &eventpair_1[idx]));
        // We don't transfer ownership, just in case close many fails, and we can try
        // closing each handle individually when the test scope exits.
        handles[idx + kNumEventpairsInvalid] = eventpair_0[idx].get();
    }

    // Duplicate the values at the start.
    handles[0u] = handles[kNumEventpairsInvalid];
    handles[1u] = handles[kNumEventpairsInvalid + 1u];


    // This returns an error value: the duplicated handles
    // can't be closed twice. Despite this, all handles were closed.
    ASSERT_EQ(zx_handle_close_many(handles, kNumEventpairCombos +
              kNumEventpairsInvalid), ZX_ERR_BAD_HANDLE);

    // Assert that every handle in the preceding close call was in
    // fact closed, by waiting on the PEER_CLOSED signal.
    for (const auto& eventpair : eventpair_1) {
            ASSERT_NO_FATAL_FAILURES(PeerWasClosed(eventpair));
    }
}

} // namespace
