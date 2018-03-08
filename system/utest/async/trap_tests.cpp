// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/trap.h>

#include <unittest/unittest.h>

#include "async_stub.h"

namespace {

class MockAsync : public AsyncStub {
public:
    async_guest_bell_trap_t* last_trap = nullptr;

    zx_status_t SetGuestBellTrap(async_guest_bell_trap_t* trap) override {
        last_trap = trap;
        return ZX_OK;
    }
};

class Handler {
public:
    void HandleGuestBellTrap(async_t* async, const zx_packet_guest_bell_t* bell) {
        handler_ran = true;
        last_bell = bell;
    }

    bool handler_ran = false;
    const zx_packet_guest_bell_t* last_bell = nullptr;
};

bool guest_bell_trap_test() {
    const zx_handle_t dummy_handle = static_cast<zx_handle_t>(1);
    const zx_vaddr_t dummy_addr = 0x1000;
    const size_t dummy_length = 0x1000;
    const zx_packet_guest_bell_t dummy_bell{
        .addr = dummy_addr,
        .reserved0 = 0u,
        .reserved1 = 0u,
        .reserved2 = 0u,
    };

    BEGIN_TEST;
    Handler handler;

    {
        async::GuestBellTrapMethod<Handler, &Handler::HandleGuestBellTrap> default_trap(&handler);
        EXPECT_EQ(ZX_HANDLE_INVALID, default_trap.guest());
        EXPECT_EQ(0u, default_trap.addr());
        EXPECT_EQ(0u, default_trap.length());
        default_trap.set_guest(dummy_handle);
        EXPECT_EQ(dummy_handle, default_trap.guest());
        default_trap.set_addr(dummy_addr);
        EXPECT_EQ(dummy_addr, default_trap.addr());
        default_trap.set_length(dummy_length);
        EXPECT_EQ(dummy_length, default_trap.length());
    }

    {
        async::GuestBellTrapMethod<Handler, &Handler::HandleGuestBellTrap> explicit_trap(
            &handler, dummy_handle, dummy_addr, dummy_length);
        EXPECT_EQ(dummy_handle, explicit_trap.guest());
        EXPECT_EQ(dummy_addr, explicit_trap.addr());
        EXPECT_EQ(dummy_length, explicit_trap.length());

        MockAsync async;
        EXPECT_EQ(ZX_OK, explicit_trap.Begin(&async));
        EXPECT_EQ(dummy_handle, async.last_trap->guest);
        EXPECT_EQ(dummy_addr, async.last_trap->addr);
        EXPECT_EQ(dummy_length, async.last_trap->length);

        async.last_trap->handler(&async, async.last_trap, &dummy_bell);
        EXPECT_TRUE(handler.handler_ran);
        EXPECT_EQ(&dummy_bell, handler.last_bell);
    }

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(trap_tests)
RUN_TEST(guest_bell_trap_test)
END_TEST_CASE(trap_tests)
