// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <set>

#include <fbl/algorithm.h>
#include <fuchsia/device/manager/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/coding.h>
#include <unittest/unittest.h>
#include <zircon/fidl.h>

#include "coordinator.h"
#include "devmgr.h"

namespace devmgr {
    zx::channel fs_clone(const char* path) {
        return zx::channel();
    }
}

static devmgr::CoordinatorConfig default_config(async_dispatcher_t* dispatcher) {
    devmgr::CoordinatorConfig config;
    config.dispatcher = dispatcher;
    config.require_system = false;
    config.asan_drivers = false;
    return config;
}

bool initialize_core_devices() {
    BEGIN_TEST;

    devmgr::Coordinator coordinator(default_config(nullptr));

    zx_status_t status = coordinator.InitializeCoreDevices();
    ASSERT_EQ(ZX_OK, status);

    END_TEST;
}

bool open_virtcon() {
    BEGIN_TEST;

    devmgr::Coordinator coordinator(default_config(nullptr));

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

    devmgr::Coordinator coordinator(default_config(nullptr));

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

bool find_loadable_drivers() {
    BEGIN_TEST;

    bool found_driver = false;
    auto callback = [&found_driver](devmgr::Driver* drv, const char* version) {
        delete drv;
        found_driver = true;
    };
    devmgr::find_loadable_drivers("/boot/driver/test", callback);
    ASSERT_TRUE(found_driver);

    END_TEST;
}

bool bind_drivers() {
    BEGIN_TEST;

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    devmgr::Coordinator coordinator(default_config(loop.dispatcher()));

    zx_status_t status = coordinator.InitializeCoreDevices();
    ASSERT_EQ(ZX_OK, status);
    coordinator.set_running(true);

    std::set<const devmgr::Driver*> drivers;
    auto callback = [&coordinator, &drivers](devmgr::Driver* drv, const char* version) {
        drivers.insert(drv);
        return coordinator.DriverAdded(drv, version);
    };
    devmgr::find_loadable_drivers("/boot/driver/test", callback);
    loop.RunUntilIdle();
    ASSERT_FALSE(coordinator.drivers().is_empty());
    for (const devmgr::Driver& drv : coordinator.drivers()) {
        ASSERT_TRUE(drivers.find(&drv) != drivers.end());
    }

    END_TEST;
}

bool bind_devices() {
    BEGIN_TEST;

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    devmgr::Coordinator coordinator(default_config(loop.dispatcher()));

    zx_status_t status = coordinator.InitializeCoreDevices();
    ASSERT_EQ(ZX_OK, status);

    // Initialize devfs.
    devmgr::devfs_init(&coordinator.root_device(), loop.dispatcher());
    status = devmgr::devfs_publish(&coordinator.root_device(), &coordinator.test_device());
    ASSERT_EQ(ZX_OK, status);
    coordinator.set_running(true);

    // Add the device.
    zx::channel local, remote;
    status = zx::channel::create(0, &local, &remote);
    ASSERT_EQ(ZX_OK, status);
    status = coordinator.AddDevice(&coordinator.test_device(), std::move(local),
                                   nullptr /* props_data */, 0 /* props_count */,
                                   "mock-device", ZX_PROTOCOL_TEST, nullptr /* driver_path */,
                                   nullptr /* args */, false /* invisible */,
                                   zx::channel() /* client_remote */);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_EQ(1, coordinator.devices().size_slow());

    // Add the driver.
    devmgr::find_loadable_drivers("/boot/driver/test",
                                  fit::bind_member(&coordinator,
                                                   &devmgr::Coordinator::DriverAdded));
    loop.RunUntilIdle();
    ASSERT_FALSE(coordinator.drivers().is_empty());

    // Bind the device to a fake devhost.
    devmgr::Device* dev = &coordinator.devices().front();
    devmgr::Devhost host;
    dev->host = &host;
    status = coordinator.BindDevice(dev, "/boot/driver/test/mock-device.so");
    ASSERT_EQ(ZX_OK, status);

    // Wait for the BindDriver request.
    zx_signals_t pending;
    status = remote.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), &pending);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_TRUE(pending & ZX_CHANNEL_READABLE);

    // Read the BindDriver request.
    uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    uint32_t actual_bytes;
    uint32_t actual_handles;
    status = remote.read(0, bytes, sizeof(bytes), &actual_bytes, handles, fbl::count_of(handles),
                         &actual_handles);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_LT(0, actual_bytes);
    ASSERT_EQ(1, actual_handles);
    status = zx_handle_close(handles[0]);
    ASSERT_EQ(ZX_OK, status);

    // Validate the BindDriver request.
    auto hdr = reinterpret_cast<fidl_message_header_t*>(bytes);
    ASSERT_EQ(fuchsia_device_manager_ControllerBindDriverOrdinal, hdr->ordinal);
    status = fidl_decode(&fuchsia_device_manager_ControllerBindDriverRequestTable, bytes,
                         actual_bytes, handles, actual_handles, nullptr);
    ASSERT_EQ(ZX_OK, status);
    auto req = reinterpret_cast<fuchsia_device_manager_ControllerBindDriverRequest*>(bytes);
    ASSERT_STR_EQ("/boot/driver/test/mock-device.so", req->driver_path.data);

    // Reset the fake devhost connection.
    dev->host = nullptr;
    remote.reset();
    loop.RunUntilIdle();

    END_TEST;
}

BEGIN_TEST_CASE(coordinator_tests)
RUN_TEST(initialize_core_devices)
RUN_TEST(open_virtcon)
RUN_TEST(dump_state)
RUN_TEST(find_loadable_drivers)
RUN_TEST(bind_drivers)
RUN_TEST(bind_devices)
END_TEST_CASE(coordinator_tests)
