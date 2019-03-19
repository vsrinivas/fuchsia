// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/driver.h>
#include <fbl/algorithm.h>
#include <fbl/vector.h>
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

// Reads a BindDriver request from remote, checks that it is for the expected
// driver, and then sends a ZX_OK response.
void CheckBindDriverReceived(const zx::channel& remote, const char* expected_driver) {
    // Read the BindDriver request.
    FIDL_ALIGNDECL uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    uint32_t actual_bytes;
    uint32_t actual_handles;
    zx_status_t status = remote.read(0, bytes, sizeof(bytes), &actual_bytes, handles,
                                     fbl::count_of(handles), &actual_handles);
    ASSERT_OK(status);
    ASSERT_LT(0, actual_bytes);
    ASSERT_EQ(1, actual_handles);
    status = zx_handle_close(handles[0]);
    ASSERT_OK(status);

    // Validate the BindDriver request.
    auto hdr = reinterpret_cast<fidl_message_header_t*>(bytes);
    ASSERT_EQ(fuchsia_device_manager_ControllerBindDriverOrdinal, hdr->ordinal);
    status = fidl_decode(&fuchsia_device_manager_ControllerBindDriverRequestTable, bytes,
                         actual_bytes, handles, actual_handles, nullptr);
    ASSERT_OK(status);
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
    ASSERT_OK(status);
    ASSERT_EQ(0, actual_handles);
    status = remote.write(0, bytes, sizeof(*resp), nullptr, 0);
    ASSERT_OK(status);
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

    // Check the BindDriver request.
    ASSERT_NO_FATAL_FAILURES(CheckBindDriverReceived(remote, kDriverPath));
    loop.RunUntilIdle();

    // Reset the fake devhost connection.
    dev->set_host(nullptr);
    remote.reset();
    loop.RunUntilIdle();
}

// Reads a CreateDevice from remote, checks expectations, and sends a ZX_OK
// response.
void CheckCreateDeviceReceived(const zx::channel& remote, const char* expected_driver,
                               zx::channel* device_remote) {
    // Read the CreateDevice request.
    FIDL_ALIGNDECL uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    uint32_t actual_bytes;
    uint32_t actual_handles;
    zx_status_t status = remote.read(0, bytes, sizeof(bytes), &actual_bytes, handles,
                                     fbl::count_of(handles), &actual_handles);
    ASSERT_OK(status);
    ASSERT_LT(0, actual_bytes);
    ASSERT_EQ(3, actual_handles);
    *device_remote = zx::channel(handles[0]);
    status = zx_handle_close(handles[1]);
    ASSERT_OK(status);

    // Validate the CreateDevice request.
    auto hdr = reinterpret_cast<fidl_message_header_t*>(bytes);
    ASSERT_EQ(fuchsia_device_manager_ControllerCreateDeviceOrdinal, hdr->ordinal);
    status = fidl_decode(&fuchsia_device_manager_ControllerCreateDeviceRequestTable, bytes,
                         actual_bytes, handles, actual_handles, nullptr);
    ASSERT_OK(status);
    auto req = reinterpret_cast<fuchsia_device_manager_ControllerCreateDeviceRequest*>(
            bytes);
    ASSERT_EQ(req->driver_path.size, strlen(expected_driver));
    ASSERT_BYTES_EQ(reinterpret_cast<const uint8_t*>(expected_driver),
                    reinterpret_cast<const uint8_t*>(req->driver_path.data), req->driver_path.size,
                    "");
}

// Reads a CreateCompositeDevice from remote, checks expectations, and sends
// a ZX_OK response.
void CheckCreateCompositeDeviceReceived(const zx::channel& remote, const char* expected_name,
                                        size_t expected_components_count) {
    // Read the CreateCompositeDevice request.
    FIDL_ALIGNDECL uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    uint32_t actual_bytes;
    uint32_t actual_handles;
    zx_status_t status = remote.read(0, bytes, sizeof(bytes), &actual_bytes, handles,
                                     fbl::count_of(handles), &actual_handles);
    ASSERT_OK(status);
    ASSERT_LT(0, actual_bytes);
    ASSERT_EQ(1, actual_handles);
    // Close the RPC channel to the device, since we don't actually need it
    status = zx_handle_close(handles[0]);
    ASSERT_OK(status);

    // Validate the CreateCompositeDevice request.
    auto hdr = reinterpret_cast<fidl_message_header_t*>(bytes);
    ASSERT_EQ(fuchsia_device_manager_ControllerCreateCompositeDeviceOrdinal, hdr->ordinal);
    status = fidl_decode(&fuchsia_device_manager_ControllerCreateCompositeDeviceRequestTable, bytes,
                         actual_bytes, handles, actual_handles, nullptr);
    ASSERT_OK(status);
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
    ASSERT_OK(status);
    ASSERT_EQ(0, actual_handles);
    status = remote.write(0, bytes, sizeof(*resp), nullptr, 0);
    ASSERT_OK(status);
}

// Helper for BindComposite for issuing an AddComposite for a composite with the
// given components.  It's assumed that these components are children of
// the platform_bus and have the given protocol_id
void BindCompositeDefineComposite(const fbl::RefPtr<devmgr::Device>& platform_bus,
                                  const uint32_t* protocol_ids, size_t component_count,
                                  const zx_device_prop_t* props, size_t props_count,
                                  const char* name, zx_status_t expected_status = ZX_OK) {
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
              expected_status);
}

struct DeviceState {
    // The representation in the coordinator of the device
    fbl::RefPtr<devmgr::Device> device;
    // The remote end of the channel that the coordinator is talking to
    zx::channel remote;
};

class CompositeTestCase : public zxtest::Test {
public:
    ~CompositeTestCase() override = default;

    async::Loop* loop() { return &loop_; }
    devmgr::Coordinator* coordinator() { return &coordinator_; }

    devmgr::Devhost* devhost() { return &devhost_; }
    const zx::channel& devhost_remote() { return devhost_remote_; }

    const fbl::RefPtr<devmgr::Device>& platform_bus() const { return platform_bus_.device; }
    DeviceState* device(size_t index) const { return &devices_[index]; }

    void AddDevice(const fbl::RefPtr<devmgr::Device>& parent, const char* name,
                   uint32_t protocol_id, fbl::String driver, size_t* device_index);

protected:
    void SetUp() override {
        ASSERT_NO_FATAL_FAILURES(InitializeCoordinator(&coordinator_));
        ASSERT_NOT_NULL(coordinator_.component_driver());

        // refcount starts at zero, so bump it up to keep us from being cleaned up
        devhost_.AddRef();
        {
            zx::channel local;
            zx_status_t status = zx::channel::create(0, &local, &devhost_remote_);
            ASSERT_EQ(ZX_OK, status);
            devhost_.set_hrpc(local.release());
        }

        // Set up the sys device proxy, inside of the devhost
        ASSERT_EQ(coordinator_.PrepareProxy(coordinator_.sys_device(), &devhost_), ZX_OK);
        loop_.RunUntilIdle();
        ASSERT_NO_FATAL_FAILURES(CheckCreateDeviceReceived(devhost_remote_, kSystemDriverPath,
                                                           &sys_proxy_remote_));
        loop_.RunUntilIdle();

        // Create a child of the sys_device, since only directly children of it can
        // issue AddComposite
        {
            zx::channel local;
            zx_status_t status = zx::channel::create(0, &local, &platform_bus_.remote);
            ASSERT_EQ(ZX_OK, status);
            status = coordinator_.AddDevice(coordinator_.sys_device()->proxy, std::move(local),
                                           nullptr /* props_data */, 0 /* props_count */,
                                           "platform-bus", 0, nullptr /* driver_path */,
                                           nullptr /* args */, false /* invisible */,
                                           zx::channel() /* client_remote */,
                                           &platform_bus_.device);
            ASSERT_EQ(ZX_OK, status);
            loop_.RunUntilIdle();
        }
    }

    void TearDown() override {
        loop_.RunUntilIdle();
        // Remove the devices in the opposite order that we added them
        while (!devices_.is_empty()) {
            devices_.pop_back();
            loop_.RunUntilIdle();
        }
        platform_bus_.device.reset();
        loop_.RunUntilIdle();

        devhost_.devices().clear();
    }
private:
    async::Loop loop_{&kAsyncLoopConfigNoAttachToThread};
    devmgr::Coordinator coordinator_{DefaultConfig(loop_.dispatcher())};

    // The fake devhost that the platform bus is put into
    devmgr::Devhost devhost_;
    // The remote end of the channel that the coordinator uses to talk to the
    // devhost
    zx::channel devhost_remote_;

    // The remote end of the channel that the coordinator uses to talk to the
    // sys device proxy
    zx::channel sys_proxy_remote_;

    // The device object representing the platform bus driver (child of the
    // sys proxy)
    DeviceState platform_bus_;

    // A list of all devices that were added during this test, and their
    // channels.  These exist to keep them alive until the test is over.
    fbl::Vector<DeviceState> devices_;
};

void CompositeTestCase::AddDevice(const fbl::RefPtr<devmgr::Device>& parent, const char* name,
                                     uint32_t protocol_id, fbl::String driver, size_t* index) {
    DeviceState state;

    zx::channel local;
    zx_status_t status = zx::channel::create(0, &local, &state.remote);
    ASSERT_EQ(ZX_OK, status);
    status = coordinator_.AddDevice(parent, std::move(local),
                                    nullptr /* props_data */, 0 /* props_count */, name,
                                    protocol_id, driver.data() /* driver_path */,
                                    nullptr /* args */, false /* invisible */,
                                    zx::channel() /* client_remote */, &state.device);
    ASSERT_EQ(ZX_OK, status);
    loop_.RunUntilIdle();

    devices_.push_back(std::move(state));
    *index = devices_.size() - 1;
}

class CompositeAddOrderTestCase : public CompositeTestCase {
public:
    enum class AddLocation {
        // Add the composite before any components
        BEFORE,
        // Add the composite after some components
        MIDDLE,
        // Add the composite after all components
        AFTER,
    };
    void ExecuteTest(AddLocation add);
};

void CompositeAddOrderTestCase::ExecuteTest(AddLocation add) {
    size_t device_indexes[3];
    uint32_t protocol_id[] = {
        ZX_PROTOCOL_GPIO,
        ZX_PROTOCOL_I2C,
        ZX_PROTOCOL_ETHERNET,
    };
    static_assert(fbl::count_of(protocol_id) == fbl::count_of(device_indexes));

    const char* kCompositeDevName = "composite-dev";
    auto do_add = [&]() {
        ASSERT_NO_FATAL_FAILURES(BindCompositeDefineComposite(
                platform_bus(), protocol_id, fbl::count_of(protocol_id), nullptr /* props */,
                0, kCompositeDevName));
    };

    if (add == AddLocation::BEFORE) {
        ASSERT_NO_FATAL_FAILURES(do_add());
    }

    // Add the devices to construct the composite out of.
    for (size_t i = 0; i < fbl::count_of(device_indexes); ++i) {
        char name[32];
        snprintf(name, sizeof(name), "device-%zu", i);
        ASSERT_NO_FATAL_FAILURES(AddDevice(platform_bus(), name, protocol_id[i], "",
                                           &device_indexes[i]));
        if (i == 0 && add == AddLocation::MIDDLE) {
            ASSERT_NO_FATAL_FAILURES(do_add());
        }
    }

    if (add == AddLocation::AFTER) {
        ASSERT_NO_FATAL_FAILURES(do_add());
    }

    size_t component_device_indexes[fbl::count_of(device_indexes)];
    for (size_t i = 0; i < fbl::count_of(device_indexes); ++i) {
        auto device_state = device(device_indexes[i]);
        // Check that the components got bound
        fbl::String driver = coordinator()->component_driver()->libname;
        ASSERT_NO_FATAL_FAILURES(CheckBindDriverReceived(device_state->remote, driver.data()));
        loop()->RunUntilIdle();

        // Synthesize the AddDevice request the component driver would send
        char name[32];
        snprintf(name, sizeof(name), "component-device-%zu", i);
        ASSERT_NO_FATAL_FAILURES(AddDevice(device_state->device, name, 0,
                                           driver, &component_device_indexes[i]));
    }

    ASSERT_NO_FATAL_FAILURES(CheckCreateCompositeDeviceReceived(devhost_remote(), kCompositeDevName,
                                                                fbl::count_of(device_indexes)));
}

TEST_F(CompositeAddOrderTestCase, DefineBeforeDevices) {
    ASSERT_NO_FATAL_FAILURES(ExecuteTest(AddLocation::BEFORE));
}

TEST_F(CompositeAddOrderTestCase, DefineInbetweenDevices) {
    ASSERT_NO_FATAL_FAILURES(ExecuteTest(AddLocation::MIDDLE));
}

TEST_F(CompositeAddOrderTestCase, DefineAfterDevices) {
    ASSERT_NO_FATAL_FAILURES(ExecuteTest(AddLocation::AFTER));
}

TEST_F(CompositeTestCase, CantAddFromNonPlatformBus) {
    size_t index;
    ASSERT_NO_FATAL_FAILURES(AddDevice(platform_bus(), "test-device", 0, "", &index));
    auto device_state = device(index);

    uint32_t protocol_id[] = { ZX_PROTOCOL_I2C, ZX_PROTOCOL_GPIO };
    ASSERT_NO_FATAL_FAILURES(BindCompositeDefineComposite(
            device_state->device, protocol_id, fbl::count_of(protocol_id), nullptr /* props */,
            0, "composite-dev", ZX_ERR_ACCESS_DENIED));
}

} // namespace

int main(int argc, char** argv) {
    return RUN_ALL_TESTS(argc, argv);
}
