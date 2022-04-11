// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.test.logger/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/driver.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/coding.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/message_builder.h>
#include <string.h>
#include <threads.h>
#include <zircon/fidl.h>

#include <vector>

#include <fbl/algorithm.h>
#include <fbl/vector.h>
#include <zxtest/zxtest.h>

#include "coordinator_test_utils.h"
#include "devfs.h"
#include "driver_host.h"
#include "fdio.h"
#include "src/devices/bin/driver_manager/fake_driver_index.h"

constexpr char kDriverPath[] = "/pkg/driver/mock-device.so";

static CoordinatorConfig NullConfig() { return DefaultConfig(nullptr, nullptr, nullptr); }

namespace {

class FidlTransaction : public fidl::Transaction {
 public:
  FidlTransaction(FidlTransaction&&) = default;
  explicit FidlTransaction(zx_txid_t transaction_id, zx::unowned_channel channel)
      : txid_(transaction_id), channel_(channel) {}

  std::unique_ptr<fidl::Transaction> TakeOwnership() override {
    return std::make_unique<FidlTransaction>(std::move(*this));
  }

  zx_status_t Reply(fidl::OutgoingMessage* message, fidl::WriteOptions write_options) override {
    ZX_ASSERT(txid_ != 0);
    message->set_txid(txid_);
    txid_ = 0;
    message->Write(channel_, std::move(write_options));
    return message->status();
  }

  void Close(zx_status_t epitaph) override { ZX_ASSERT(false); }

  void InternalError(fidl::UnbindInfo info, fidl::ErrorOrigin origin) override {
    detected_error_ = info;
  }

  ~FidlTransaction() override = default;

  const std::optional<fidl::UnbindInfo>& detected_error() const { return detected_error_; }

 private:
  zx_txid_t txid_;
  zx::unowned_channel channel_;
  std::optional<fidl::UnbindInfo> detected_error_;
};

class FakeDevice : public fidl::WireServer<fuchsia_device_manager::DeviceController> {
 public:
  FakeDevice(fidl::ServerEnd<fuchsia_driver_test_logger::Logger> test_output,
             const fidl::StringView expected_driver = {})
      : test_output_(std::move(test_output)), expected_driver_(expected_driver) {}

  void BindDriver(BindDriverRequestView request, BindDriverCompleter::Sync& completer) override {
    if (expected_driver_.empty() || expected_driver_.get() == request->driver_path.get()) {
      bind_called_ = true;
      completer.Reply(ZX_OK, test_output_.TakeChannel());
    } else {
      completer.Reply(ZX_ERR_INTERNAL, zx::channel{});
    }
  }
  void ConnectProxy(ConnectProxyRequestView request,
                    ConnectProxyCompleter::Sync& _completer) override {}
  void Init(InitRequestView request, InitCompleter::Sync& completer) override {}
  void Suspend(SuspendRequestView request, SuspendCompleter::Sync& completer) override {}
  void Resume(ResumeRequestView request, ResumeCompleter::Sync& completer) override {}
  void Unbind(UnbindRequestView request, UnbindCompleter::Sync& completer) override {}
  void CompleteRemoval(CompleteRemovalRequestView request,
                       CompleteRemovalCompleter::Sync& completer) override {}
  void Open(OpenRequestView request, OpenCompleter::Sync& _completer) override {}

  bool bind_called() { return bind_called_; }

 private:
  fidl::ServerEnd<fuchsia_driver_test_logger::Logger> test_output_;
  const fidl::StringView expected_driver_;
  bool bind_called_ = false;
};

// Reads a BindDriver request from remote, checks that it is for the expected
// driver, and then sends a ZX_OK response.
void CheckBindDriverReceived(
    const fidl::ServerEnd<fuchsia_device_manager::DeviceController>& controller,
    const fidl::StringView expected_driver) {
  uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  fidl_channel_handle_metadata_t handle_metadata[ZX_CHANNEL_MAX_MSG_HANDLES];
  fidl::IncomingMessage msg =
      fidl::MessageRead(controller.channel(), fidl::BufferSpan(bytes, std::size(bytes)), handles,
                        handle_metadata, ZX_CHANNEL_MAX_MSG_HANDLES);
  ASSERT_TRUE(msg.ok());

  auto* header = msg.header();
  FidlTransaction txn(header->txid, zx::unowned(controller.channel()));

  FakeDevice fake(fidl::ServerEnd<fuchsia_driver_test_logger::Logger>(), expected_driver);
  fidl::WireDispatch(
      static_cast<fidl::WireServer<fuchsia_device_manager::DeviceController>*>(&fake),
      std::move(msg), &txn);
  ASSERT_FALSE(txn.detected_error());
  ASSERT_TRUE(fake.bind_called());
}

}  // namespace

TEST(MiscTestCase, InitCoreDevices) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  InspectManager inspect_manager(loop.dispatcher());
  Coordinator coordinator(NullConfig(), &inspect_manager, loop.dispatcher(), loop.dispatcher());
  coordinator.InitCoreDevices(kSystemDriverPath);
}

TEST(MiscTestCase, DumpState) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  InspectManager inspect_manager(loop.dispatcher());

  Coordinator coordinator(NullConfig(), &inspect_manager, loop.dispatcher(), loop.dispatcher());
  coordinator.InitCoreDevices(kSystemDriverPath);

  constexpr int32_t kBufSize = 256;
  char buf[kBufSize + 1] = {0};

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kBufSize, 0, &vmo));
  VmoWriter writer(std::move(vmo));

  coordinator.debug_dump()->DumpState(&writer);

  ASSERT_EQ(writer.written(), writer.available());
  ASSERT_LT(writer.written(), kBufSize);
  ASSERT_GT(writer.written(), 0);
  ASSERT_OK(writer.vmo().read(buf, 0, writer.written()));

  ASSERT_NE(nullptr, strstr(buf, "[root]"));
}

TEST(MiscTestCase, LoadDriver) {
  bool found_driver = false;
  auto callback = [&found_driver](Driver* drv, const char* version) {
    delete drv;
    found_driver = true;
  };
  load_driver(nullptr, kDriverPath, callback);
  ASSERT_TRUE(found_driver);
}

TEST(MiscTestCase, LoadDisabledDriver) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread("mock-boot-args"));
  mock_boot_arguments::Server boot_args{{{"driver.mock_device.disable", "true"}}};
  fidl::WireSyncClient<fuchsia_boot::Arguments> client;
  boot_args.CreateClient(loop.dispatcher(), &client);

  bool found_driver = false;
  auto callback = [&found_driver](Driver* drv, const char* version) {
    delete drv;
    found_driver = true;
  };
  load_driver(&client, kDriverPath, callback);
  ASSERT_FALSE(found_driver);
}

TEST(MiscTestCase, BindDrivers) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  InspectManager inspect_manager(loop.dispatcher());
  Coordinator coordinator(NullConfig(), &inspect_manager, loop.dispatcher(), loop.dispatcher());

  coordinator.InitCoreDevices(kSystemDriverPath);
  coordinator.set_running(true);

  Driver* driver;
  auto callback = [&coordinator, &driver](Driver* drv, const char* version) {
    driver = drv;
    return coordinator.DriverAdded(drv, version);
  };
  load_driver(nullptr, kDriverPath, callback);
  loop.RunUntilIdle();
  ASSERT_EQ(1, coordinator.drivers().size_slow());
  ASSERT_EQ(driver, &coordinator.drivers().front());
}

TEST(MiscTestCase, BindDevices) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  InspectManager inspect_manager(loop.dispatcher());
  Coordinator coordinator(NullConfig(), &inspect_manager, loop.dispatcher(), loop.dispatcher());

  ASSERT_NO_FATAL_FAILURE(InitializeCoordinator(&coordinator));

  // Add the device.
  auto controller_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::DeviceController>();
  ASSERT_OK(controller_endpoints.status_value());

  auto coordinator_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::Coordinator>();
  ASSERT_OK(coordinator_endpoints.status_value());

  fbl::RefPtr<Device> device;
  auto status = coordinator.device_manager()->AddDevice(
      coordinator.sys_device(), std::move(controller_endpoints->client),
      std::move(coordinator_endpoints->server), nullptr /* props_data */, 0 /* props_count */,
      nullptr /* str_props_data */, 0 /* str_props_count */, "mock-device", ZX_PROTOCOL_TEST,
      {} /* driver_path */, {} /* args */, false /* skip_autobind */, false /* has_init */,
      true /* always_init */, zx::vmo() /*inspect*/, zx::channel() /* client_remote */,
      fidl::ClientEnd<fio::Directory>() /* outgoing_dir */, &device);
  ASSERT_OK(status);
  ASSERT_EQ(1, coordinator.device_manager()->devices().size_slow());

  // Add the driver.
  load_driver(nullptr, kDriverPath, fit::bind_member(&coordinator, &Coordinator::DriverAdded));
  loop.RunUntilIdle();
  ASSERT_FALSE(coordinator.drivers().is_empty());

  // The device has no driver_host, so the init task should automatically complete.
  ASSERT_TRUE(device->is_visible());
  ASSERT_EQ(Device::State::kActive, device->state());

  // Bind the device to a fake driver_host.
  fbl::RefPtr<Device> dev = fbl::RefPtr(&coordinator.device_manager()->devices().front());
  auto host = fbl::MakeRefCounted<DriverHost>(
      &coordinator, fidl::ClientEnd<fuchsia_device_manager::DriverHostController>(),
      fidl::ClientEnd<fuchsia_io::Directory>(), zx::process{});
  dev->set_host(std::move(host));
  status = coordinator.bind_driver_manager()->BindDevice(dev, kDriverPath, true /* new device */);
  ASSERT_OK(status);

  // Check the BindDriver request.
  ASSERT_NO_FATAL_FAILURE(
      CheckBindDriverReceived(controller_endpoints->server, fidl::StringView(kDriverPath)));
  loop.RunUntilIdle();

  // Reset the fake driver_host connection.
  dev->set_host(nullptr);
  coordinator_endpoints->client.reset();
  controller_endpoints->server.reset();
  loop.RunUntilIdle();
}

void CompareStrProperty(const fuchsia_device_manager::wire::DeviceStrProperty expected,
                        const StrProperty actual) {
  ASSERT_STREQ(expected.key.get(), actual.key);

  if (expected.value.is_int_value()) {
    auto* value = std::get_if<StrPropValueType::Integer>(&actual.value);
    ASSERT_TRUE(value);
    ASSERT_EQ(expected.value.int_value(), *value);
  } else if (expected.value.is_str_value()) {
    auto* value = std::get_if<StrPropValueType::String>(&actual.value);
    ASSERT_TRUE(value);
    ASSERT_STREQ(expected.value.str_value(), *value);
  } else if (expected.value.is_bool_value()) {
    auto* value = std::get_if<StrPropValueType::Bool>(&actual.value);
    ASSERT_TRUE(value);
    ASSERT_EQ(expected.value.bool_value(), *value);
  } else if (expected.value.is_enum_value()) {
    auto* value = std::get_if<StrPropValueType::Enum>(&actual.value);
    ASSERT_TRUE(value);
    ASSERT_STREQ(expected.value.enum_value(), *value);
  }
}

// Adds a device with the given properties to the device coordinator, then checks that the
// coordinator contains the device, and that its properties are correct.
void AddDeviceWithProperties(const fuchsia_device_manager::wire::DeviceProperty* props_data,
                             size_t props_count,
                             const fuchsia_device_manager::wire::DeviceStrProperty* str_props_data,
                             size_t str_props_count) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  InspectManager inspect_manager(loop.dispatcher());
  Coordinator coordinator(NullConfig(), &inspect_manager, loop.dispatcher(), loop.dispatcher());

  ASSERT_NO_FATAL_FAILURE(InitializeCoordinator(&coordinator));

  auto controller_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::DeviceController>();
  ASSERT_OK(controller_endpoints.status_value());

  auto coordinator_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::Coordinator>();
  ASSERT_OK(coordinator_endpoints.status_value());

  fbl::RefPtr<Device> device;
  auto status = coordinator.device_manager()->AddDevice(
      coordinator.sys_device(), std::move(controller_endpoints->client),
      std::move(coordinator_endpoints->server), props_data, props_count, str_props_data,
      str_props_count, "mock-device", ZX_PROTOCOL_TEST, {} /* driver_path */, {} /* args */,
      false /* skip_autobind */, false /* has_init */, true /* always_init */,
      zx::vmo() /*inspect*/, zx::channel() /* client_remote */,
      fidl::ClientEnd<fio::Directory>() /* outgoing_dir */, &device);
  ASSERT_OK(status);

  // Check that the device has been added to the coordinator, with the correct properties.
  ASSERT_EQ(1, coordinator.device_manager()->devices().size_slow());
  const Device& dev = coordinator.device_manager()->devices().front();
  ASSERT_EQ(dev.props().size(), props_count);
  for (size_t i = 0; i < props_count; i++) {
    ASSERT_EQ(dev.props()[i].id, props_data[i].id);
    ASSERT_EQ(dev.props()[i].reserved, props_data[i].reserved);
    ASSERT_EQ(dev.props()[i].value, props_data[i].value);
  }

  ASSERT_EQ(dev.str_props().size(), str_props_count);
  for (size_t i = 0; i < str_props_count; i++) {
    CompareStrProperty(str_props_data[i], dev.str_props()[i]);
  }

  controller_endpoints->server.reset();
  coordinator_endpoints->client.reset();
  loop.RunUntilIdle();
}

TEST(MiscTestCase, DeviceProperties) {
  // No properties.
  AddDeviceWithProperties(nullptr, 0, nullptr, 0);

  // Multiple device properties. No string properties.
  fuchsia_device_manager::wire::DeviceProperty props[] = {
      fuchsia_device_manager::wire::DeviceProperty{1, 0, 1},
      fuchsia_device_manager::wire::DeviceProperty{2, 0, 1},
  };
  AddDeviceWithProperties(props, std::size(props), nullptr, 0);

  uint32_t int_val = 1000;
  auto str_val = fidl::StringView::FromExternal("timberdoodle");

  // Multiple device string properties. No device properties.
  fuchsia_device_manager::wire::DeviceStrProperty str_props[] = {
      fuchsia_device_manager::wire::DeviceStrProperty{
          "snipe", fuchsia_device_manager::wire::PropertyValue::WithStrValue(
                       fidl::ObjectView<fidl::StringView>::FromExternal(&str_val))},
      fuchsia_device_manager::wire::DeviceStrProperty{
          "sandpiper", fuchsia_device_manager::wire::PropertyValue::WithIntValue(int_val)},
  };
  AddDeviceWithProperties(nullptr, 0, str_props, std::size(str_props));

  // Multiple device properties and device string properties.
  AddDeviceWithProperties(props, std::size(props), str_props, std::size(str_props));
}

TEST(MiscTestCase, InvalidStringProperties) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  InspectManager inspect_manager(loop.dispatcher());
  Coordinator coordinator(NullConfig(), &inspect_manager, loop.dispatcher(), loop.dispatcher());

  ASSERT_NO_FATAL_FAILURE(InitializeCoordinator(&coordinator));

  auto controller_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::DeviceController>();
  ASSERT_OK(controller_endpoints.status_value());

  auto coordinator_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::Coordinator>();
  ASSERT_OK(coordinator_endpoints.status_value());

  // Create an invalid string with invalid UTF-8 characters.
  const char kInvalidStr[] = {static_cast<char>(0xC0), 0};
  auto str_val = fidl::StringView::FromExternal("ovenbird");
  fuchsia_device_manager::wire::DeviceStrProperty str_props[] = {
      fuchsia_device_manager::wire::DeviceStrProperty{
          fidl::StringView::FromExternal(kInvalidStr, std::size(kInvalidStr)),
          fuchsia_device_manager::wire::PropertyValue::WithStrValue(
              fidl::ObjectView<fidl::StringView>::FromExternal(&str_val))},
  };

  fbl::RefPtr<Device> device;
  auto status = coordinator.device_manager()->AddDevice(
      coordinator.sys_device(), std::move(controller_endpoints->client),
      std::move(coordinator_endpoints->server), nullptr /* props */, 0 /* props_count */, str_props,
      std::size(str_props), "mock-device", ZX_PROTOCOL_TEST, {} /* driver_path */, {} /* args */,
      false /* skip_autobind */, false /* has_init */, true /* always_init */,
      zx::vmo() /*inspect*/, zx::channel() /* client_remote */,
      fidl::ClientEnd<fio::Directory>() /* outgoing_dir */, &device);
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, status);
}

TEST(MiscTestCase, DeviceAlreadyBoundFromDriverIndex) {
  constexpr const char kFakeDriverUrl[] = "#driver/mock-device.so";
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  async::Loop index_loop(&kAsyncLoopConfigNeverAttachToThread);
  ASSERT_OK(index_loop.StartThread("test-thread"));
  InspectManager inspect_manager(loop.dispatcher());
  FakeDriverIndex fake(index_loop.dispatcher(),
                       [&](auto args) -> zx::status<FakeDriverIndex::MatchResult> {
                         return zx::ok(FakeDriverIndex::MatchResult{
                             .url = kFakeDriverUrl,
                         });
                       });
  auto config = NullConfig();
  config.driver_index = fidl::WireSharedClient<fdf::DriverIndex>(std::move(fake.Connect().value()),
                                                                 loop.dispatcher());
  Coordinator coordinator(std::move(config), &inspect_manager, loop.dispatcher(),
                          loop.dispatcher());

  ASSERT_NO_FATAL_FAILURE(InitializeCoordinator(&coordinator));

  // Add the device.
  auto controller_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::DeviceController>();
  ASSERT_OK(controller_endpoints.status_value());

  auto coordinator_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::Coordinator>();
  ASSERT_OK(coordinator_endpoints.status_value());

  fbl::RefPtr<Device> device;
  auto status = coordinator.device_manager()->AddDevice(
      coordinator.sys_device(), std::move(controller_endpoints->client),
      std::move(coordinator_endpoints->server), nullptr /* props_data */, 0 /* props_count */,
      nullptr /* str_props_data */, 0 /* str_props_count */, "mock-device", ZX_PROTOCOL_TEST,
      {} /* driver_path */, {} /* args */, true /* skip_autobind */, false /* has_init */,
      true /* always_init */, zx::vmo() /*inspect*/, zx::channel() /* client_remote */,
      fidl::ClientEnd<fio::Directory>() /*outgoing_dir*/, &device);
  ASSERT_OK(status);
  ASSERT_EQ(1, coordinator.device_manager()->devices().size_slow());

  // Bind the device to a fake driver_host.
  fbl::RefPtr<Device> dev = fbl::RefPtr(&coordinator.device_manager()->devices().front());
  auto host = fbl::MakeRefCounted<DriverHost>(
      &coordinator, fidl::ClientEnd<fuchsia_device_manager::DriverHostController>(),
      fidl::ClientEnd<fuchsia_io::Directory>(), zx::process{});
  dev->set_host(std::move(host));
  status =
      coordinator.bind_driver_manager()->BindDevice(dev, kFakeDriverUrl, true /* new device */);
  ASSERT_OK(status);
  loop.RunUntilIdle();

  status =
      coordinator.bind_driver_manager()->BindDevice(dev, kFakeDriverUrl, true /* new device */);
  ASSERT_STATUS(status, ZX_ERR_ALREADY_BOUND);
  loop.RunUntilIdle();

  // Reset the fake driver_host connection.
  dev->set_host(nullptr);
  coordinator_endpoints->client.reset();
  controller_endpoints->server.reset();
  loop.RunUntilIdle();
}

TEST(MiscTestCase, AddDeviceGroup) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  async::Loop index_loop(&kAsyncLoopConfigNeverAttachToThread);
  ASSERT_OK(index_loop.StartThread("test-thread"));

  InspectManager inspect_manager(loop.dispatcher());
  FakeDriverIndex fake_driver_index(index_loop.dispatcher(),
                                    [&](auto args) -> zx::status<FakeDriverIndex::MatchResult> {
                                      return zx::error(ZX_ERR_NOT_FOUND);
                                    });

  auto config = NullConfig();
  config.driver_index = fidl::WireSharedClient<fdf::DriverIndex>(
      std::move(fake_driver_index.Connect().value()), loop.dispatcher());

  Coordinator coordinator(std::move(config), &inspect_manager, loop.dispatcher(),
                          loop.dispatcher());

  ASSERT_NO_FATAL_FAILURE(InitializeCoordinator(&coordinator));

  auto controller_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::DeviceController>();
  ASSERT_OK(controller_endpoints.status_value());

  auto coordinator_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::Coordinator>();
  ASSERT_OK(coordinator_endpoints.status_value());

  // Add the device.
  fbl::RefPtr<Device> device;
  auto status = coordinator.device_manager()->AddDevice(
      coordinator.sys_device(), std::move(controller_endpoints->client),
      std::move(coordinator_endpoints->server), nullptr /* props_data */, 0 /* props_count */,
      nullptr /* str_props_data */, 0 /* str_props_count */, "mock-device", ZX_PROTOCOL_TEST,
      {} /* driver_path */, {} /* args */, true /* skip_autobind */, false /* has_init */,
      true /* always_init */, zx::vmo() /*inspect*/, zx::channel() /* client_remote */,
      fidl::ClientEnd<fio::Directory>() /*outgoing_dir*/, &device);
  ASSERT_OK(status);
  ASSERT_EQ(1, coordinator.device_manager()->devices().size_slow());

  fidl::Arena allocator;
  fidl::VectorView<fuchsia_device_manager::wire::DeviceStrProperty> str_props(allocator, 2);
  str_props[0] = fuchsia_device_manager::wire::DeviceStrProperty{
      fidl::StringView(allocator, "scoter"),
      fuchsia_device_manager::wire::PropertyValue::WithStrValue(
          allocator, fidl::StringView(allocator, "bufflehead"))};
  str_props[1] = fuchsia_device_manager::wire::DeviceStrProperty{
      fidl::StringView(allocator, "merganser"),
      fuchsia_device_manager::wire::PropertyValue::WithIntValue(1000)};

  fidl::VectorView<fuchsia_device_manager::wire::DeviceProperty> props(allocator, 2);
  props[0] = fuchsia_device_manager::wire::DeviceProperty{1, 0, 1};
  props[1] = fuchsia_device_manager::wire::DeviceProperty{2, 0, 1};

  fidl::VectorView<fdf::wire::DeviceGroupProperty> node_properties(allocator, 2);

  auto prop_vals_1 = fidl::VectorView<fdf::wire::NodePropertyValue>(allocator, 1);
  prop_vals_1[0] = fdf::wire::NodePropertyValue::WithBoolValue(false);
  node_properties[0] = fdf::wire::DeviceGroupProperty{
      .key = fdf::wire::NodePropertyKey::WithIntValue(100),
      .condition = fdf::wire::Condition::kAccept,
      .values = prop_vals_1,
  };

  auto prop_vals_2 = fidl::VectorView<fdf::wire::NodePropertyValue>(allocator, 1);
  prop_vals_2[0] = fdf::wire::NodePropertyValue::WithIntValue(20);
  node_properties[1] = fdf::wire::DeviceGroupProperty{
      .key = fdf::wire::NodePropertyKey::WithIntValue(5),
      .condition = fdf::wire::Condition::kAccept,
      .values = prop_vals_2,
  };

  fidl::VectorView<fdf::wire::DeviceGroupNode> fragments(allocator, 1);
  fragments[0] = fdf::wire::DeviceGroupNode{
      .name = fidl::StringView(allocator, "mallard"),
      .properties = node_properties,
  };

  fidl::VectorView<fuchsia_device_manager::wire::DeviceMetadata> metadata(allocator, 0);

  fuchsia_device_manager::wire::DeviceGroupDescriptor group_desc =
      fuchsia_device_manager::wire::DeviceGroupDescriptor{
          .props = props,
          .str_props = str_props,
          .fragments = fragments,
          .spawn_colocated = false,
          .metadata = metadata,
      };

  ASSERT_OK(coordinator.AddDeviceGroup(device, "group", group_desc));
  loop.RunUntilIdle();
  ZX_ASSERT(
      coordinator.bind_driver_manager()->device_groups().count("/dev/sys/mock-device/group") != 0);

  controller_endpoints->server.reset();
  coordinator_endpoints->client.reset();
  loop.RunUntilIdle();
}

int main(int argc, char** argv) { return RUN_ALL_TESTS(argc, argv); }
