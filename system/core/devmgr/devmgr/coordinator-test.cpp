// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <unittest/unittest.h>

#include "coordinator.h"

namespace devmgr {
    zx::channel fs_clone(const char* path) {
        return zx::channel();
    }
}

static devmgr::CoordinatorConfig default_config() {
    devmgr::CoordinatorConfig config;
    config.dispatcher = nullptr;
    config.require_system = false;
    config.asan_drivers = false;
    return config;
}

bool initialize_core_devices() {
    BEGIN_TEST;

    devmgr::Coordinator coordinator(default_config());

    zx_status_t status = coordinator.InitializeCoreDevices();
    ASSERT_EQ(ZX_OK, status);

    END_TEST;
}

bool open_virtcon() {
    BEGIN_TEST;

    devmgr::Coordinator coordinator(default_config());

    zx::channel client, server;
    zx_status_t status = zx::channel::create(0, &client, &server);
    ASSERT_EQ(ZX_OK, status);
    coordinator.set_virtcon_channel(std::move(client));

    zx::channel sender, receiver;
    status = zx::channel::create(0, &sender, &receiver);
    ASSERT_EQ(ZX_OK, status);
    status = coordinator.OpenVirtcon(std::move(sender));
    ASSERT_EQ(ZX_OK, status);

    zx_signals_t signals;
    status = server.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), &signals);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_TRUE(signals & ZX_CHANNEL_READABLE);

    zx::channel sender_channel;
    uint32_t actual_handles;
    status = server.read(0, nullptr, 0, nullptr, sender_channel.reset_and_get_address(), 1,
                         &actual_handles);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_EQ(1, actual_handles);
    ASSERT_TRUE(sender_channel.is_valid());

    END_TEST;
}

bool dump_state() {
    BEGIN_TEST;

    devmgr::Coordinator coordinator(default_config());

    zx_status_t status = coordinator.InitializeCoreDevices();
    ASSERT_EQ(ZX_OK, status);

    zx::socket client, server;
    status = zx::socket::create(0, &client, &server);
    ASSERT_EQ(ZX_OK, status);
    coordinator.set_dmctl_socket(std::move(client));
    coordinator.DumpState();

    zx_signals_t signals;
    status = server.wait_one(ZX_SOCKET_READABLE, zx::time::infinite(), &signals);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_TRUE(signals & ZX_SOCKET_READABLE);

    uint8_t buf[256];
    size_t actual;
    status = server.read(0, buf, sizeof(buf), &actual);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_NE(0, actual);

    END_TEST;
}

BEGIN_TEST_CASE(coordinator_tests)
RUN_TEST(initialize_core_devices)
RUN_TEST(open_virtcon)
RUN_TEST(dump_state)
END_TEST_CASE(coordinator_tests)
