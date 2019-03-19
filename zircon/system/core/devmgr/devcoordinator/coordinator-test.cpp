// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/driver.h>
#include <fbl/algorithm.h>
#include <fuchsia/device/manager/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/coding.h>
#include <zircon/fidl.h>
#include <zxtest/zxtest.h>

#include "coordinator.h"
#include "devfs.h"
#include "devhost.h"

namespace devmgr {
zx::channel fs_clone(const char* path) {
    return zx::channel();
}
} // namespace devmgr

namespace {

constexpr char kSystemDriverPath[] = "/boot/driver/platform-bus.so";
constexpr char kDriverPath[] = "/boot/driver/test/mock-device.so";

devmgr::CoordinatorConfig DefaultConfig(async_dispatcher_t* dispatcher) {
    devmgr::CoordinatorConfig config{};
    config.dispatcher = dispatcher;
    config.require_system = false;
    config.asan_drivers = false;
    return config;
}

TEST(CoordinatorTestCase, InitializeCoreDevices) {
    devmgr::Coordinator coordinator(DefaultConfig(nullptr));

    zx_status_t status = coordinator.InitializeCoreDevices(kSystemDriverPath);
    ASSERT_EQ(ZX_OK, status);
}

TEST(CoordinatorTestCase, OpenVirtcon) {
    devmgr::Coordinator coordinator(DefaultConfig(nullptr));

    zx::channel client, server;
    zx_status_t status = zx::channel::create(0, &client, &server);
    ASSERT_EQ(ZX_OK, status);
    coordinator.set_virtcon_channel(std::move(client));

    zx::channel sender, receiver;
    status = zx::channel::create(0, &sender, &receiver);
    ASSERT_EQ(ZX_OK, status);
    status = coordinator.DmOpenVirtcon(std::move(sender));
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
}

TEST(CoordinatorTestCase, DumpState) {
    devmgr::Coordinator coordinator(DefaultConfig(nullptr));

    zx_status_t status = coordinator.InitializeCoreDevices(kSystemDriverPath);
    ASSERT_EQ(ZX_OK, status);

    constexpr int32_t kBufSize = 256;
    char buf[kBufSize + 1] = {0};

    zx::vmo vmo;
    ASSERT_EQ(ZX_OK, zx::vmo::create(kBufSize, 0, &vmo));
    devmgr::VmoWriter writer(std::move(vmo));

    coordinator.DumpState(&writer);

    ASSERT_EQ(writer.written(), writer.available());
    ASSERT_LT(writer.written(), kBufSize);
    ASSERT_GT(writer.written(), 0);
    ASSERT_EQ(ZX_OK, writer.vmo().read(buf, 0, writer.written()));

    ASSERT_NE(nullptr, strstr(buf, "[root]"));
}

TEST(CoordinatorTestCase, LoadDriver) {
    bool found_driver = false;
    auto callback = [&found_driver](devmgr::Driver* drv, const char* version) {
        delete drv;
        found_driver = true;
    };
    devmgr::load_driver(kDriverPath, callback);
    ASSERT_TRUE(found_driver);
}

TEST(CoordinatorTestCase, BindDrivers) {
    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    devmgr::Coordinator coordinator(DefaultConfig(loop.dispatcher()));

    zx_status_t status = coordinator.InitializeCoreDevices(kSystemDriverPath);
    ASSERT_EQ(ZX_OK, status);
    coordinator.set_running(true);

    devmgr::Driver* driver;
    auto callback = [&coordinator, &driver](devmgr::Driver* drv, const char* version) {
        driver = drv;
        return coordinator.DriverAdded(drv, version);
    };
    devmgr::load_driver(kDriverPath, callback);
    loop.RunUntilIdle();
    ASSERT_EQ(1, coordinator.drivers().size_slow());
    ASSERT_EQ(driver, &coordinator.drivers().front());
}

void InitializeCoordinator(devmgr::Coordinator* coordinator) {
    zx_status_t status = coordinator->InitializeCoreDevices(kSystemDriverPath);
    ASSERT_EQ(ZX_OK, status);

    // Load the component driver
    devmgr::load_driver(devmgr::kComponentDriverPath,
                       fit::bind_member(coordinator, &devmgr::Coordinator::DriverAddedInit));

    // Add the driver we're using as platform bus
    devmgr::load_driver(kSystemDriverPath,
                       fit::bind_member(coordinator, &devmgr::Coordinator::DriverAddedInit));

    // Initialize devfs.
    devmgr::devfs_init(coordinator->root_device(), coordinator->dispatcher());
    status = devmgr::devfs_publish(coordinator->root_device(), coordinator->test_device());
    status = devmgr::devfs_publish(coordinator->root_device(), coordinator->sys_device());
    ASSERT_EQ(ZX_OK, status);
    coordinator->set_running(true);
}

// Waits for a BindDriver request to come in on remote, checks that it is for
// the expected driver, and then sends a ZX_OK response.
void CheckBindDriverReceived(const zx::channel& remote, const char* expected_driver) {
    // Wait for the BindDriver request.
    zx_signals_t pending;
    zx_status_t status = remote.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), &pending);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_TRUE(pending & ZX_CHANNEL_READABLE);

    // Read the BindDriver request.
    FIDL_ALIGNDECL uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
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
    ASSERT_EQ(req->driver_path.size, strlen(expected_driver));
    ASSERT_BYTES_EQ(reinterpret_cast<const uint8_t*>(expected_driver),
                    reinterpret_cast<const uint8_t*>(req->driver_path.data),
                    req->driver_path.size, "");

    // Write the BindDriver response.
    memset(bytes, 0, sizeof(bytes));
    auto resp = reinterpret_cast<fuchsia_device_manager_ControllerBindDriverResponse*>(bytes);
    resp->hdr.ordinal = fuchsia_device_manager_ControllerBindDriverOrdinal;
    resp->status = ZX_OK;
    status = fidl_encode(&fuchsia_device_manager_ControllerBindDriverResponseTable, bytes,
                         sizeof(*resp), handles, fbl::count_of(handles), &actual_handles, nullptr);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_EQ(0, actual_handles);
    status = remote.write(0, bytes, sizeof(*resp), nullptr, 0);
    ASSERT_EQ(ZX_OK, status);
}

TEST(CoordinatorTestCase, BindDevices) {
    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    devmgr::Coordinator coordinator(DefaultConfig(loop.dispatcher()));

    ASSERT_NO_FATAL_FAILURES(InitializeCoordinator(&coordinator));

    // Add the device.
    zx::channel local, remote;
    zx_status_t status = zx::channel::create(0, &local, &remote);
    ASSERT_EQ(ZX_OK, status);
    fbl::RefPtr<devmgr::Device> device;
    status = coordinator.AddDevice(coordinator.test_device(), std::move(local),
                                   nullptr /* props_data */, 0 /* props_count */, "mock-device",
                                   ZX_PROTOCOL_TEST, nullptr /* driver_path */, nullptr /* args */,
                                   false /* invisible */, zx::channel() /* client_remote */,
                                   &device);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_EQ(1, coordinator.devices().size_slow());

    // Add the driver.
    devmgr::load_driver(kDriverPath,
                       fit::bind_member(&coordinator, &devmgr::Coordinator::DriverAdded));
    loop.RunUntilIdle();
    ASSERT_FALSE(coordinator.drivers().is_empty());

    // Bind the device to a fake devhost.
    fbl::RefPtr<devmgr::Device> dev = fbl::WrapRefPtr(&coordinator.devices().front());
    devmgr::Devhost host;
    host.AddRef(); // refcount starts at zero, so bump it up to keep us from being cleaned up
    dev->set_host(&host);
    status = coordinator.BindDevice(dev, kDriverPath, true /* new device */);
    ASSERT_EQ(ZX_OK, status);

    // Wait for the BindDriver request.
    ASSERT_NO_FATAL_FAILURES(CheckBindDriverReceived(remote, kDriverPath));
    loop.RunUntilIdle();

    // Reset the fake devhost connection.
    dev->set_host(nullptr);
    remote.reset();
    loop.RunUntilIdle();
}

// Waits for a CreateDevice request to come in on remote, checks
// expectations, and sends a ZX_OK response.
void CheckCreateDeviceReceived(const zx::channel& remote, const char* expected_driver,
                               zx::channel* device_remote) {
    // Wait for the CreateDevice request.
    zx_signals_t pending;
    zx_status_t status = remote.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), &pending);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_TRUE(pending & ZX_CHANNEL_READABLE);

    // Read the CreateDevice request.
    FIDL_ALIGNDECL uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    uint32_t actual_bytes;
    uint32_t actual_handles;
    status = remote.read(0, bytes, sizeof(bytes), &actual_bytes, handles, fbl::count_of(handles),
                         &actual_handles);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_LT(0, actual_bytes);
    ASSERT_EQ(2, actual_handles);
    *device_remote = zx::channel(handles[0]);
    status = zx_handle_close(handles[1]);
    ASSERT_EQ(ZX_OK, status);

    // Validate the CreateDevice request.
    auto hdr = reinterpret_cast<fidl_message_header_t*>(bytes);
    ASSERT_EQ(fuchsia_device_manager_ControllerCreateDeviceOrdinal, hdr->ordinal);
    status = fidl_decode(&fuchsia_device_manager_ControllerCreateDeviceRequestTable, bytes,
                         actual_bytes, handles, actual_handles, nullptr);
    ASSERT_EQ(ZX_OK, status);
    auto req = reinterpret_cast<fuchsia_device_manager_ControllerCreateDeviceRequest*>(
            bytes);
    ASSERT_EQ(req->driver_path.size, strlen(expected_driver));
    ASSERT_BYTES_EQ(reinterpret_cast<const uint8_t*>(expected_driver),
                    reinterpret_cast<const uint8_t*>(req->driver_path.data), req->driver_path.size,
                    "");
}

// Waits for a CreateCompositeDevice request to come in on remote, checks
// expectations, and sends a ZX_OK response.
void CheckCreateCompositeDeviceReceived(const zx::channel& remote, const char* expected_name,
                                        size_t expected_components_count) {
    // Wait for the CreateCompositeDevice request.
    zx_signals_t pending;
    zx_status_t status = remote.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), &pending);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_TRUE(pending & ZX_CHANNEL_READABLE);

    // Read the CreateCompositeDevice request.
    FIDL_ALIGNDECL uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    uint32_t actual_bytes;
    uint32_t actual_handles;
    status = remote.read(0, bytes, sizeof(bytes), &actual_bytes, handles, fbl::count_of(handles),
                         &actual_handles);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_LT(0, actual_bytes);
    ASSERT_EQ(1, actual_handles);
    // Close the RPC channel to the device, since we don't actually need it
    status = zx_handle_close(handles[0]);
    ASSERT_EQ(ZX_OK, status);

    // Validate the CreateCompositeDevice request.
    auto hdr = reinterpret_cast<fidl_message_header_t*>(bytes);
    ASSERT_EQ(fuchsia_device_manager_ControllerCreateCompositeDeviceOrdinal, hdr->ordinal);
    status = fidl_decode(&fuchsia_device_manager_ControllerCreateCompositeDeviceRequestTable, bytes,
                         actual_bytes, handles, actual_handles, nullptr);
    ASSERT_EQ(ZX_OK, status);
    auto req = reinterpret_cast<fuchsia_device_manager_ControllerCreateCompositeDeviceRequest*>(
            bytes);
    ASSERT_EQ(req->name.size, strlen(expected_name));
    ASSERT_BYTES_EQ(reinterpret_cast<const uint8_t*>(expected_name),
                    reinterpret_cast<const uint8_t*>(req->name.data), req->name.size, "");
    ASSERT_EQ(expected_components_count, req->components.count);

    // Write the CreateCompositeDevice response.
    memset(bytes, 0, sizeof(bytes));
    auto resp = reinterpret_cast<fuchsia_device_manager_ControllerCreateCompositeDeviceResponse*>(
            bytes);
    resp->hdr.ordinal = fuchsia_device_manager_ControllerCreateCompositeDeviceOrdinal;
    resp->status = ZX_OK;
    status = fidl_encode(&fuchsia_device_manager_ControllerCreateCompositeDeviceResponseTable,
                         bytes, sizeof(*resp), handles, fbl::count_of(handles), &actual_handles,
                         nullptr);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_EQ(0, actual_handles);
    status = remote.write(0, bytes, sizeof(*resp), nullptr, 0);
    ASSERT_EQ(ZX_OK, status);
}

// Helper for BindComposite for issuing an AddComposite for a composite with the
// given components.  It's assumed that these components are children of
// the platform_bus and have the given protocol_id
void BindCompositeDefineComposite(const fbl::RefPtr<devmgr::Device>& platform_bus,
                                  const uint32_t* protocol_ids, size_t component_count,
                                  const zx_device_prop_t* props, size_t props_count,
                                  const char* name) {
    auto components = std::make_unique<fuchsia_device_manager_DeviceComponent[]>(component_count);
    for (size_t i = 0; i < component_count; ++i) {
        // Define a union type to avoid violating the strict aliasing rule.
        union InstValue {
            zx_bind_inst_t inst;
            uint64_t value;
        };
        InstValue always = {.inst = BI_MATCH()};
        InstValue protocol = {.inst = BI_MATCH_IF(EQ, BIND_PROTOCOL, protocol_ids[i])};

        fuchsia_device_manager_DeviceComponent* component = &components[i];
        component->parts_count = 2;
        component->parts[0].match_program_count = 1;
        component->parts[0].match_program[0] = always.value;
        component->parts[1].match_program_count = 1;
        component->parts[1].match_program[0] = protocol.value;
    }
    devmgr::Coordinator* coordinator = platform_bus->coordinator;
    ASSERT_EQ(coordinator->AddCompositeDevice(platform_bus, name, props, props_count,
                                              components.get(), component_count,
                                              0 /* coresident index */),
              ZX_OK);
}

void BindCompositeImpl(bool define_composite_before_devices) {
    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    devmgr::Coordinator coordinator(DefaultConfig(loop.dispatcher()));

    ASSERT_NO_FATAL_FAILURES(InitializeCoordinator(&coordinator));
    ASSERT_NOT_NULL(coordinator.component_driver());

    // Create a mock devhost and connect the test device to it, so that all of
    // its children show up there
    devmgr::Devhost host;
    zx::channel devhost_remote;
    zx::channel platform_bus_devhost_remote;
    host.AddRef(); // refcount starts at zero, so bump it up to keep us from being cleaned up
    {
        zx::channel local;
        zx_status_t status = zx::channel::create(0, &local, &devhost_remote);
        ASSERT_EQ(ZX_OK, status);
        host.set_hrpc(local.release());
    }

    // Set up the sys device proxy
    ASSERT_EQ(coordinator.PrepareProxy(coordinator.sys_device(), &host), ZX_OK);
    loop.RunUntilIdle();
    ASSERT_NO_FATAL_FAILURES(CheckCreateDeviceReceived(devhost_remote, kSystemDriverPath,
                                                       &platform_bus_devhost_remote));
    loop.RunUntilIdle();

    // Create a child of the sys_device, since only directly children of it can
    // issue AddComposite
    fbl::RefPtr<devmgr::Device> platform_bus;
    zx::channel platform_bus_remote;
    {
        zx::channel local;
        zx_status_t status = zx::channel::create(0, &local, &platform_bus_remote);
        ASSERT_EQ(ZX_OK, status);
        status = coordinator.AddDevice(coordinator.sys_device()->proxy, std::move(local),
                                       nullptr /* props_data */, 0 /* props_count */,
                                       "platform-bus", 0, nullptr /* driver_path */,
                                       nullptr /* args */, false /* invisible */,
                                       zx::channel() /* client_remote */, &platform_bus);
        ASSERT_EQ(ZX_OK, status);
        ASSERT_EQ(1, coordinator.devices().size_slow());
        loop.RunUntilIdle();
    }

    fbl::RefPtr<devmgr::Device> devices[2];
    zx::channel device_rpcs[fbl::count_of(devices)];
    uint32_t protocol_id[] = {
        ZX_PROTOCOL_GPIO,
        ZX_PROTOCOL_I2C,
    };
    static_assert(fbl::count_of(protocol_id) == fbl::count_of(devices));

    const char* kCompositeDevName = "composite-dev";
    if (define_composite_before_devices) {
        ASSERT_NO_FATAL_FAILURES(BindCompositeDefineComposite(
                platform_bus, protocol_id, fbl::count_of(protocol_id), nullptr /* props */,
                0, kCompositeDevName));
    }

    // Add the devices to construct the composite out of.
    for (size_t i = 0; i < fbl::count_of(devices); ++i) {
        zx::channel local;
        zx_status_t status = zx::channel::create(0, &local, &device_rpcs[i]);
        ASSERT_EQ(ZX_OK, status);
        char name[32];
        snprintf(name, sizeof(name), "device-%zu", i);
        status = coordinator.AddDevice(platform_bus, std::move(local),
                                       nullptr /* props_data */, 0 /* props_count */, name,
                                       protocol_id[i], nullptr /* driver_path */,
                                       nullptr /* args */, false /* invisible */,
                                       zx::channel() /* client_remote */, &devices[i]);
        ASSERT_EQ(ZX_OK, status);
        ASSERT_EQ(i + 2, coordinator.devices().size_slow());
        loop.RunUntilIdle();
    }

    if (!define_composite_before_devices) {
        ASSERT_NO_FATAL_FAILURES(BindCompositeDefineComposite(
                platform_bus, protocol_id, fbl::count_of(protocol_id), nullptr /* props */,
                0, kCompositeDevName));
    }

    fbl::RefPtr<devmgr::Device> component_devices[fbl::count_of(devices)];
    zx::channel component_device_rpcs[fbl::count_of(devices)];
    for (size_t i = 0; i < fbl::count_of(devices); ++i) {
        // Wait for the components to get bound
        fbl::String driver = coordinator.component_driver()->libname;
        ASSERT_NO_FATAL_FAILURES(CheckBindDriverReceived(device_rpcs[i], driver.data()));
        loop.RunUntilIdle();

        // Synthesize the AddDevice request the component driver would send
        zx::channel local, remote;
        zx_status_t status = zx::channel::create(0, &local, &component_device_rpcs[i]);
        ASSERT_EQ(ZX_OK, status);
        char name[32];
        snprintf(name, sizeof(name), "composite-device-%zu", i);
        status = coordinator.AddDevice(devices[i], std::move(local),
                                       nullptr /* props_data */, 0 /* props_count */, name,
                                       ZX_PROTOCOL_COMPOSITE, driver.data(),
                                       nullptr /* args */, false /* invisible */,
                                       zx::channel() /* client_remote */, &component_devices[i]);
        ASSERT_EQ(ZX_OK, status);
        loop.RunUntilIdle();
    }

    ASSERT_NO_FATAL_FAILURES(CheckCreateCompositeDeviceReceived(devhost_remote, kCompositeDevName,
                                                                fbl::count_of(devices)));

    loop.RunUntilIdle();
    host.devices().clear();
}

TEST(CoordinatorTestCase, BindCompositeDefineAfterDevices) {
    ASSERT_NO_FATAL_FAILURES(BindCompositeImpl(false));
}

TEST(CoordinatorTestCase, BindCompositeDefineBeforeDevices) {
    ASSERT_NO_FATAL_FAILURES(BindCompositeImpl(true));
}

} // namespace

int main(int argc, char** argv) {
    return RUN_ALL_TESTS(argc, argv);
}
