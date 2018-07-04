// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-testutils/dispatcher_stub.h>
#include <lib/async/cpp/trap.h>
#include <unittest/unittest.h>

namespace {

const zx_handle_t dummy_guest = static_cast<zx_handle_t>(1);
const zx_vaddr_t dummy_addr = 0x1000;
const size_t dummy_length = 0x1000;
const zx_packet_guest_bell_t dummy_bell{
    .addr = dummy_addr,
    .reserved0 = 0u,
    .reserved1 = 0u,
    .reserved2 = 0u,
};

class MockDispatcher : public async::DispatcherStub {
public:
    zx_status_t SetGuestBellTrap(async_guest_bell_trap_t* trap,
                                 const zx::guest& guest,
                                 zx_vaddr_t addr, size_t length) override {
        last_trap = trap;
        last_guest = guest.get();
        last_addr = addr;
        last_length = length;
        return ZX_OK;
    }

    async_guest_bell_trap_t* last_trap = nullptr;
    zx_handle_t last_guest = ZX_HANDLE_INVALID;
    zx_vaddr_t last_addr = 0u;
    size_t last_length = 0u;
};

class Harness {
public:
    void Handler(async_dispatcher_t* dispatcher,
                 async::GuestBellTrapBase* trap,
                 zx_status_t status,
                 const zx_packet_guest_bell_t* bell) {
        handler_ran = true;
        last_trap = trap;
        last_status = status;
        last_bell = bell;
    }

    virtual async::GuestBellTrapBase& trap() = 0;

    bool handler_ran = false;
    async::GuestBellTrapBase* last_trap = nullptr;
    zx_status_t last_status = ZX_ERR_INTERNAL;
    const zx_packet_guest_bell_t* last_bell = nullptr;
};

class LambdaHarness : public Harness {
public:
    async::GuestBellTrapBase& trap() override { return trap_; }

private:
    async::GuestBellTrap trap_{[this](async_dispatcher_t* dispatcher, async::GuestBellTrap* trap,
                                      zx_status_t status, const zx_packet_guest_bell_t* bell) {
        Handler(dispatcher, trap, status, bell);
    }};
};

class MethodHarness : public Harness {
public:
    async::GuestBellTrapBase& trap() override { return trap_; }

private:
    async::GuestBellTrapMethod<Harness, &Harness::Handler> trap_{this};
};

bool guest_bell_trap_set_handler_test() {
    BEGIN_TEST;

    {
        async::GuestBellTrap trap;
        EXPECT_FALSE(trap.has_handler());

        trap.set_handler([](async_dispatcher_t* dispatcher, async::GuestBellTrap* trap,
                            zx_status_t status, const zx_packet_guest_bell_t* bell) {});
        EXPECT_TRUE(trap.has_handler());
    }

    {
        async::GuestBellTrap trap([](async_dispatcher_t* dispatcher, async::GuestBellTrap* trap,
                                     zx_status_t status, const zx_packet_guest_bell_t* bell) {});
        EXPECT_TRUE(trap.has_handler());
    }

    END_TEST;
}

template <typename Harness>
bool guest_bell_trap_test() {
    BEGIN_TEST;

    MockDispatcher dispatcher;
    Harness harness;

    EXPECT_EQ(ZX_OK, harness.trap().SetTrap(&dispatcher, zx::unowned_guest::wrap(dummy_guest),
                                            dummy_addr, dummy_length));
    EXPECT_EQ(dummy_guest, dispatcher.last_guest);
    EXPECT_EQ(dummy_addr, dispatcher.last_addr);
    EXPECT_EQ(dummy_length, dispatcher.last_length);

    dispatcher.last_trap->handler(&dispatcher, dispatcher.last_trap, ZX_OK, &dummy_bell);
    EXPECT_TRUE(harness.handler_ran);
    EXPECT_EQ(&harness.trap(), harness.last_trap);
    EXPECT_EQ(ZX_OK, harness.last_status);
    EXPECT_EQ(&dummy_bell, harness.last_bell);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(trap_tests)
RUN_TEST(guest_bell_trap_set_handler_test)
RUN_TEST((guest_bell_trap_test<LambdaHarness>))
RUN_TEST((guest_bell_trap_test<MethodHarness>))
END_TEST_CASE(trap_tests)
