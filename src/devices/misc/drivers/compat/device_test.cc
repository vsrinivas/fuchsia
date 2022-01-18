// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/misc/drivers/compat/device.h"

#include <fidl/fuchsia.driver.framework/cpp/wire_test_base.h>
#include <lib/ddk/metadata.h>
#include <lib/gtest/test_loop_fixture.h>

#include <gtest/gtest.h>

namespace fdf = fuchsia_driver_framework;
namespace fio = fuchsia_io;
namespace frunner = fuchsia_component_runner;

class TestNode : public fdf::testing::Node_TestBase {
 public:
  void Clear() {
    controllers_.clear();
    nodes_.clear();
  }

 private:
  void AddChild(AddChildRequestView request, AddChildCompleter::Sync& completer) override {
    controllers_.push_back(std::move(request->controller));
    nodes_.push_back(std::move(request->node));
    completer.ReplySuccess();
  }

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override {
    printf("Not implemented: Node::%s\n", name.data());
  }

  std::vector<fidl::ServerEnd<fdf::NodeController>> controllers_;
  std::vector<fidl::ServerEnd<fdf::Node>> nodes_;
};

class DeviceTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();

    auto svc = fidl::CreateEndpoints<fio::Directory>();
    ASSERT_EQ(ZX_OK, svc.status_value());
    auto ns = CreateNamespace(std::move(svc->client));
    ASSERT_EQ(ZX_OK, ns.status_value());

    auto logger = driver::Logger::Create(*ns, dispatcher(), "test-logger");
    ASSERT_EQ(ZX_OK, logger.status_value());
    logger_ = std::move(*logger);
  }

 protected:
  driver::Logger& logger() { return logger_; }

 private:
  zx::status<driver::Namespace> CreateNamespace(fidl::ClientEnd<fio::Directory> client_end) {
    fidl::Arena arena;
    fidl::VectorView<frunner::wire::ComponentNamespaceEntry> entries(arena, 1);
    entries[0].Allocate(arena);
    entries[0].set_path(arena, "/svc").set_directory(std::move(client_end));
    return driver::Namespace::Create(entries);
  }

  driver::Logger logger_;
};

TEST_F(DeviceTest, ConstructDevice) {
  auto endpoints = fidl::CreateEndpoints<fdf::Node>();

  // Create a device.
  zx_protocol_device_t ops{};
  compat::Device device("test-device", nullptr, &ops, std::nullopt, std::nullopt, logger(),
                        dispatcher());
  device.Bind({std::move(endpoints->client), dispatcher()});

  // Test basic functions on the device.
  EXPECT_EQ(reinterpret_cast<uintptr_t>(&device), reinterpret_cast<uintptr_t>(device.ZxDevice()));
  EXPECT_STREQ("test-device", device.Name());
  EXPECT_FALSE(device.HasChildren());

  // Create a node to test device unbind.
  TestNode node;
  fidl::BindServer(dispatcher(), std::move(endpoints->server), &node,
                   [](auto, fidl::UnbindInfo info, auto) {
                     EXPECT_EQ(fidl::Reason::kPeerClosed, info.reason());
                   });
  device.Unbind();

  ASSERT_TRUE(RunLoopUntilIdle());
}

TEST_F(DeviceTest, AddChildDevice) {
  auto endpoints = fidl::CreateEndpoints<fdf::Node>();

  // Create a node.
  TestNode node;
  auto binding = fidl::BindServer(dispatcher(), std::move(endpoints->server), &node);

  // Create a device.
  zx_protocol_device_t ops{};
  compat::Device parent("parent", nullptr, &ops, std::nullopt, std::nullopt, logger(),
                        dispatcher());
  parent.Bind({std::move(endpoints->client), dispatcher()});

  // Add a child device.
  device_add_args_t args{.name = "child"};
  zx_device_t* child = nullptr;
  zx_status_t status = parent.Add(&args, &child);
  ASSERT_EQ(ZX_OK, status);
  EXPECT_NE(nullptr, child);
  EXPECT_STREQ("child", child->Name());
  EXPECT_TRUE(parent.HasChildren());

  // Ensure that AddChild was executed.
  ASSERT_TRUE(RunLoopUntilIdle());
}

TEST_F(DeviceTest, AddChildDeviceWithInit) {
  auto endpoints = fidl::CreateEndpoints<fdf::Node>();

  // Create a node.
  TestNode node;
  auto binding = fidl::BindServer(dispatcher(), std::move(endpoints->server), &node);

  // Create a device.
  zx_protocol_device_t parent_ops{};
  compat::Device parent("parent", nullptr, &parent_ops, std::nullopt, std::nullopt, logger(),
                        dispatcher());
  parent.Bind({std::move(endpoints->client), dispatcher()});

  // Add a child device.
  bool child_ctx = false;
  zx_protocol_device_t child_ops{
      .init = [](void* ctx) { *static_cast<bool*>(ctx) = true; },
  };
  device_add_args_t args{
      .name = "child",
      .ctx = &child_ctx,
      .ops = &child_ops,
  };
  zx_device_t* child = nullptr;
  zx_status_t status = parent.Add(&args, &child);
  ASSERT_EQ(ZX_OK, status);
  EXPECT_NE(nullptr, child);
  EXPECT_STREQ("child", child->Name());
  EXPECT_TRUE(parent.HasChildren());

  // Check that the init hook was run.
  EXPECT_FALSE(child_ctx);
  ASSERT_TRUE(RunLoopUntilIdle());
  EXPECT_TRUE(child_ctx);
}

TEST_F(DeviceTest, AddAndRemoveChildDevice) {
  auto endpoints = fidl::CreateEndpoints<fdf::Node>();

  // Create a node.
  TestNode node;
  auto binding = fidl::BindServer(dispatcher(), std::move(endpoints->server), &node);

  // Create a device.
  zx_protocol_device_t ops{};
  compat::Device parent("parent", nullptr, &ops, std::nullopt, std::nullopt, logger(),
                        dispatcher());
  parent.Bind({std::move(endpoints->client), dispatcher()});

  // Add a child device.
  device_add_args_t args{.name = "child"};
  zx_device_t* child = nullptr;
  zx_status_t status = parent.Add(&args, &child);
  ASSERT_EQ(ZX_OK, status);
  EXPECT_NE(nullptr, child);
  EXPECT_STREQ("child", child->Name());
  EXPECT_TRUE(parent.HasChildren());

  // Remove the child device.
  child->Remove();
  ASSERT_TRUE(RunLoopUntilIdle());

  // Emulate the removal of the node, and check that the related child device is
  // removed from the parent device.
  EXPECT_TRUE(parent.HasChildren());
  node.Clear();
  ASSERT_TRUE(RunLoopUntilIdle());
  EXPECT_FALSE(parent.HasChildren());
}

TEST_F(DeviceTest, GetProtocolFromDevice) {
  // Create a device without a get_protocol hook.
  zx_protocol_device_t ops{};
  compat::Device without("without-protocol", nullptr, &ops, std::nullopt, std::nullopt, logger(),
                         dispatcher());
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, without.GetProtocol(ZX_PROTOCOL_BLOCK, nullptr));

  // Create a device with a get_protocol hook.
  ops.get_protocol = [](void* ctx, uint32_t proto_id, void* protocol) {
    EXPECT_EQ(ZX_PROTOCOL_BLOCK, proto_id);
    return ZX_OK;
  };
  compat::Device with("with-protocol", nullptr, &ops, std::nullopt, std::nullopt, logger(),
                      dispatcher());
  ASSERT_EQ(ZX_OK, with.GetProtocol(ZX_PROTOCOL_BLOCK, nullptr));
}

TEST_F(DeviceTest, DeviceMetadata) {
  // Create a device.
  zx_protocol_device_t ops{};
  compat::Device device("test-device", nullptr, &ops, std::nullopt, std::nullopt, logger(),
                        dispatcher());

  // Add metadata to the device.
  const uint64_t metadata = 0xAABBCCDDEEFF0011;
  zx_status_t status = device.AddMetadata(DEVICE_METADATA_PRIVATE, &metadata, sizeof(metadata));
  ASSERT_EQ(ZX_OK, status);

  // Add the same metadata again.
  status = device.AddMetadata(DEVICE_METADATA_PRIVATE, &metadata, sizeof(metadata));
  ASSERT_EQ(ZX_ERR_ALREADY_EXISTS, status);

  // Check the metadata size.
  size_t size = 0;
  status = device.GetMetadataSize(DEVICE_METADATA_PRIVATE, &size);
  ASSERT_EQ(ZX_OK, status);
  EXPECT_EQ(sizeof(metadata), size);

  // Check the metadata size for missing metadata.
  status = device.GetMetadataSize(DEVICE_METADATA_BOARD_PRIVATE, &size);
  ASSERT_EQ(ZX_ERR_NOT_FOUND, status);

  // Get the metadata.
  uint64_t found = 0;
  size_t found_size = 0;
  status = device.GetMetadata(DEVICE_METADATA_PRIVATE, &found, sizeof(found), &found_size);
  ASSERT_EQ(ZX_OK, status);
  EXPECT_EQ(metadata, found);
  EXPECT_EQ(sizeof(metadata), found_size);

  // Get the metadata for missing metadata.
  status = device.GetMetadata(DEVICE_METADATA_BOARD_PRIVATE, &found, sizeof(found), &found_size);
  ASSERT_EQ(ZX_ERR_NOT_FOUND, status);
}

TEST_F(DeviceTest, LinkedDeviceMetadata) {
  // Create two devices.
  zx_protocol_device_t ops{};
  compat::Device parent("test-parent", nullptr, &ops, std::nullopt, std::nullopt, logger(),
                        dispatcher());
  compat::Device child("test-device", nullptr, &ops, std::nullopt, &parent, logger(), dispatcher());

  // Add metadata to the parent device.
  const uint64_t metadata = 0xAABBCCDDEEFF0011;
  zx_status_t status = parent.AddMetadata(DEVICE_METADATA_PRIVATE, &metadata, sizeof(metadata));
  ASSERT_EQ(ZX_OK, status);

  // Get the metadata from the child device.
  uint64_t found = 0;
  size_t found_size = 0;
  status = child.GetMetadata(DEVICE_METADATA_PRIVATE, &found, sizeof(found), &found_size);
  ASSERT_EQ(ZX_OK, status);
  EXPECT_EQ(metadata, found);
  EXPECT_EQ(sizeof(metadata), found_size);
}
