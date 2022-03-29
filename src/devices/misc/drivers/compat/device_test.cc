// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/misc/drivers/compat/device.h"

#include <fidl/fuchsia.component.runner/cpp/wire_types.h>
#include <fidl/fuchsia.device/cpp/markers.h>
#include <fidl/fuchsia.driver.framework/cpp/wire_test_base.h>
#include <fidl/test.placeholders/cpp/wire.h>
#include <lib/ddk/metadata.h>
#include <lib/gtest/test_loop_fixture.h>

#include <gtest/gtest.h>

#include "lib/ddk/binding_priv.h"
#include "lib/ddk/device.h"
#include "lib/ddk/driver.h"
#include "lib/fidl/llcpp/connect_service.h"
#include "lib/fidl/llcpp/transaction.h"
#include "lib/service/llcpp/outgoing_directory.h"
#include "src/devices/lib/compat/symbols.h"
#include "src/devices/misc/drivers/compat/devfs_vnode.h"
#include "src/devices/misc/drivers/compat/driver.h"

namespace fdf = fuchsia_driver_framework;
namespace fio = fuchsia_io;
namespace frunner = fuchsia_component_runner;

namespace {

// Simple Echo implementation used to test FIDL functionality.
class EchoImpl : public fidl::WireServer<test_placeholders::Echo>, public fs::Service {
 public:
  explicit EchoImpl(async_dispatcher_t* dispatcher)
      : fs::Service([dispatcher, this](fidl::ServerEnd<test_placeholders::Echo> server) {
          fidl::BindServer(dispatcher, std::move(server), this);
          return ZX_OK;
        }) {}
  void EchoString(EchoStringRequestView request, EchoStringCompleter::Sync& completer) override {
    completer.Reply(request->value);
  }
};

class TestController : public fidl::testing::WireTestBase<fdf::NodeController> {
 public:
  static TestController* Create(async_dispatcher_t* dispatcher,
                                fidl::ServerEnd<fdf::NodeController> server) {
    auto controller = std::make_unique<TestController>();
    TestController* p = controller.get();
    fidl::BindServer(dispatcher, std::move(server), controller.get(),
                     [controller = std::move(controller)](
                         TestController*, fidl::UnbindInfo,
                         fidl::ServerEnd<fdf::NodeController>) mutable { controller.reset(); });
    return p;
  }

  void Remove(RemoveRequestView request, RemoveCompleter::Sync& completer) override {
    completer.Close(ZX_OK);
  }

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override {
    printf("Not implemented: Controller::%s\n", name.data());
  }
};

class TestNode : public fidl::testing::WireTestBase<fdf::Node> {
 public:
  explicit TestNode(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}
  void Clear() {
    controllers_.clear();
    nodes_.clear();
  }

  void SetAddChildHook(std::function<void(AddChildRequestView& rv)> func) {
    add_child_hook_.emplace(std::move(func));
  }

  bool HasChildren() { return !nodes_.empty(); }

 private:
  void AddChild(AddChildRequestView request, AddChildCompleter::Sync& completer) override {
    if (add_child_hook_) {
      add_child_hook_.value()(request);
    }
    controllers_.push_back(TestController::Create(dispatcher_, std::move(request->controller)));
    nodes_.push_back(std::move(request->node));
    completer.ReplySuccess();
  }

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override {
    printf("Not implemented: Node::%s\n", name.data());
  }

  std::optional<std::function<void(AddChildRequestView& rv)>> add_child_hook_;
  std::vector<TestController*> controllers_;
  std::vector<fidl::ServerEnd<fdf::Node>> nodes_;
  async_dispatcher_t* dispatcher_;
};

std::optional<fdf::wire::NodePropertyValue> GetProperty(fdf::wire::NodeAddArgs& args,
                                                        fdf::wire::NodePropertyKey key) {
  if (!args.has_properties()) {
    return std::nullopt;
  }

  std::optional<fdf::wire::NodePropertyValue> ret;

  for (auto& prop : args.properties()) {
    if (!prop.has_key() || !prop.has_value()) {
      continue;
    }
    if (prop.key().Which() != key.Which() || prop.key().has_invalid_tag()) {
      continue;
    }

    if (key.is_int_value() && prop.key().int_value() != key.int_value()) {
      continue;
    }
    if (key.is_string_value()) {
      std::string_view prop_view{prop.key().string_value().data(),
                                 prop.key().string_value().size()};
      std::string_view key_view{key.string_value().data(), key.string_value().size()};
      if (prop_view != key_view) {
        continue;
      }
    }

    // We found a match. Keep iterating though, because the last property in the list of
    // properties takes precedence.
    ret = prop.value();
  }
  return ret;
}

}  // namespace

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

  std::pair<std::unique_ptr<DevfsVnode>, fidl::WireClient<fuchsia_device::Controller>> CreateVnode(
      zx_device_t* device) {
    auto vnode = std::make_unique<DevfsVnode>(device);
    auto dev_endpoints = fidl::CreateEndpoints<fuchsia_device::Controller>();
    EXPECT_EQ(ZX_OK, dev_endpoints.status_value());

    fidl::BindServer(test_loop().dispatcher(), std::move(dev_endpoints->server), vnode.get());
    fidl::WireClient<fuchsia_device::Controller> client;
    client.Bind(std::move(dev_endpoints->client), test_loop().dispatcher());

    return std::make_pair(std::move(vnode), std::move(client));
  }

  std::pair<std::unique_ptr<TestNode>, fidl::ClientEnd<fdf::Node>> CreateTestNode() {
    auto endpoints = fidl::CreateEndpoints<fdf::Node>();
    auto node = std::make_unique<TestNode>(dispatcher());

    fidl::BindServer(dispatcher(), std::move(endpoints->server), node.get());
    return std::make_pair(std::move(node), std::move(endpoints->client));
  }

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
  compat::Device device(compat::kDefaultDevice, &ops, nullptr, std::nullopt, logger(),
                        dispatcher());
  device.Bind({std::move(endpoints->client), dispatcher()});

  // Test basic functions on the device.
  EXPECT_EQ(reinterpret_cast<uintptr_t>(&device), reinterpret_cast<uintptr_t>(device.ZxDevice()));
  EXPECT_STREQ("compat-device", device.Name());
  EXPECT_FALSE(device.HasChildren());

  // Create a node to test device unbind.
  TestNode node(dispatcher());
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
  TestNode node(dispatcher());
  auto binding = fidl::BindServer(dispatcher(), std::move(endpoints->server), &node);

  // Create a device.
  zx_protocol_device_t ops{};
  compat::Device parent(compat::kDefaultDevice, &ops, nullptr, std::nullopt, logger(),
                        dispatcher());
  parent.Bind({std::move(endpoints->client), dispatcher()});

  // Add a child device.
  device_add_args_t args{.name = "child"};
  zx_device_t* child = nullptr;
  ASSERT_EQ(ZX_OK, parent.Add(&args, &child));
  ASSERT_EQ(ZX_OK, child->CreateNode());
  EXPECT_NE(nullptr, child);
  EXPECT_STREQ("child", child->Name());
  EXPECT_TRUE(parent.HasChildren());

  // Ensure that AddChild was executed.
  ASSERT_TRUE(RunLoopUntilIdle());
}

TEST_F(DeviceTest, AddChildWithProtoPropAndProtoId) {
  auto endpoints = fidl::CreateEndpoints<fdf::Node>();

  // Create a node.
  TestNode node(dispatcher());
  auto binding = fidl::BindServer(dispatcher(), std::move(endpoints->server), &node);

  // Create a device.
  zx_protocol_device_t ops{};
  compat::Device parent(compat::kDefaultDevice, &ops, nullptr, std::nullopt, logger(),
                        dispatcher());
  parent.Bind({std::move(endpoints->client), dispatcher()});

  bool ran = false;
  node.SetAddChildHook([&ran](TestNode::AddChildRequestView& rv) {
    ran = true;
    auto& prop = rv->args.properties()[0];
    ASSERT_EQ(prop.key().int_value(), (uint32_t)BIND_PROTOCOL);
    ASSERT_EQ(prop.value().int_value(), ZX_PROTOCOL_I2C);
  });

  // Add a child device.
  zx_device_prop_t prop{.id = BIND_PROTOCOL, .value = ZX_PROTOCOL_I2C};
  device_add_args_t args{
      .name = "child", .props = &prop, .prop_count = 1, .proto_id = ZX_PROTOCOL_BLOCK};
  zx_device_t* child = nullptr;
  ASSERT_EQ(ZX_OK, parent.Add(&args, &child));
  ASSERT_EQ(ZX_OK, child->CreateNode());

  EXPECT_NE(nullptr, child);
  EXPECT_STREQ("child", child->Name());
  EXPECT_TRUE(parent.HasChildren());

  ASSERT_TRUE(RunLoopUntilIdle());
  ASSERT_TRUE(ran);
}

TEST_F(DeviceTest, AddChildWithStringProps) {
  auto endpoints = fidl::CreateEndpoints<fdf::Node>();

  // Create a node.
  TestNode node(dispatcher());
  auto binding = fidl::BindServer(dispatcher(), std::move(endpoints->server), &node);

  // Create a device.
  zx_protocol_device_t ops{};
  compat::Device parent(compat::kDefaultDevice, &ops, nullptr, std::nullopt, logger(),
                        dispatcher());
  parent.Bind({std::move(endpoints->client), dispatcher()});

  bool ran = false;
  node.SetAddChildHook([&ran](TestNode::AddChildRequestView& rv) {
    ran = true;
    auto& prop = rv->args.properties()[0];
    ASSERT_EQ(strncmp(prop.key().string_value().data(), "hello", prop.key().string_value().size()),
              0);
    ASSERT_EQ(prop.value().int_value(), 1u);

    prop = rv->args.properties()[1];
    ASSERT_EQ(
        strncmp(prop.key().string_value().data(), "another", prop.key().string_value().size()), 0);
    ASSERT_EQ(prop.value().bool_value(), true);

    prop = rv->args.properties()[2];
    ASSERT_EQ(strncmp(prop.key().string_value().data(), "key", prop.key().string_value().size()),
              0);
    ASSERT_EQ(
        strncmp(prop.value().string_value().data(), "value", prop.value().string_value().size()),
        0);

    prop = rv->args.properties()[3];
    ASSERT_EQ(
        strncmp(prop.key().string_value().data(), "enum_key", prop.key().string_value().size()), 0);
    ASSERT_EQ(
        strncmp(prop.value().enum_value().data(), "enum_value", prop.value().enum_value().size()),
        0);
  });

  // Add a child device.
  zx_device_str_prop_t props[4] = {
      zx_device_str_prop_t{
          .key = "hello",
          .property_value = str_prop_int_val(1),
      },
      zx_device_str_prop_t{
          .key = "another",
          .property_value = str_prop_bool_val(true),
      },
      zx_device_str_prop_t{
          .key = "key",
          .property_value = str_prop_str_val("value"),
      },
      zx_device_str_prop_t{
          .key = "enum_key",
          .property_value = str_prop_enum_val("enum_value"),
      },
  };
  device_add_args_t args{.name = "child",
                         .str_props = props,
                         .str_prop_count = sizeof(props) / sizeof(props[0]),
                         .proto_id = ZX_PROTOCOL_BLOCK};
  zx_device_t* child = nullptr;
  ASSERT_EQ(ZX_OK, parent.Add(&args, &child));
  ASSERT_EQ(ZX_OK, child->CreateNode());
  EXPECT_NE(nullptr, child);
  EXPECT_STREQ("child", child->Name());
  EXPECT_TRUE(parent.HasChildren());

  ASSERT_TRUE(RunLoopUntilIdle());
  ASSERT_TRUE(ran);
}

TEST_F(DeviceTest, AddChildDeviceWithInit) {
  auto endpoints = fidl::CreateEndpoints<fdf::Node>();

  // Create a node.
  TestNode node(dispatcher());
  auto binding = fidl::BindServer(dispatcher(), std::move(endpoints->server), &node);

  // Create a device.
  zx_protocol_device_t parent_ops{};
  compat::Device parent(compat::kDefaultDevice, &parent_ops, nullptr, std::nullopt, logger(),
                        dispatcher());
  parent.Bind({std::move(endpoints->client), dispatcher()});

  // Add a child device.
  bool child_ctx = false;
  static zx_protocol_device_t child_ops{
      .init = [](void* ctx) { *static_cast<bool*>(ctx) = true; },
  };
  device_add_args_t args{
      .name = "child",
      .ctx = &child_ctx,
      .ops = &child_ops,
  };
  zx_device_t* child = nullptr;
  ASSERT_EQ(ZX_OK, parent.Add(&args, &child));
  ASSERT_EQ(ZX_OK, child->CreateNode());
  EXPECT_NE(nullptr, child);
  EXPECT_STREQ("child", child->Name());
  EXPECT_TRUE(parent.HasChildren());

  // Check that the init hook was run.
  EXPECT_FALSE(child_ctx);
  ASSERT_TRUE(RunLoopUntilIdle());
  EXPECT_TRUE(child_ctx);

  // Check that init promise hasn't finished yet.
  bool init_is_finished = false;
  child->executor().schedule_task(child->WaitForInitToComplete().and_then(
      [&init_is_finished]() mutable { init_is_finished = true; }));
  ASSERT_TRUE(RunLoopUntilIdle());
  EXPECT_FALSE(init_is_finished);

  // Reply to init and check that the promise finishes.
  device_init_reply(child, ZX_OK, nullptr);
  EXPECT_TRUE(RunLoopUntilIdle());
  EXPECT_TRUE(init_is_finished);
}

TEST_F(DeviceTest, AddAndRemoveChildDevice) {
  auto endpoints = fidl::CreateEndpoints<fdf::Node>();

  // Create a node.
  TestNode node(dispatcher());
  auto binding = fidl::BindServer(dispatcher(), std::move(endpoints->server), &node);

  // Create a device.
  zx_protocol_device_t ops{};
  compat::Device parent(compat::kDefaultDevice, &ops, nullptr, std::nullopt, logger(),
                        dispatcher());
  parent.Bind({std::move(endpoints->client), dispatcher()});

  // Add a child device.
  device_add_args_t args{.name = "child"};
  zx_device_t* child = nullptr;
  ASSERT_EQ(ZX_OK, parent.Add(&args, &child));
  ASSERT_EQ(ZX_OK, child->CreateNode());
  EXPECT_NE(nullptr, child);
  EXPECT_STREQ("child", child->Name());
  EXPECT_TRUE(parent.HasChildren());

  // Remove the child device.
  child->Remove();
  ASSERT_TRUE(RunLoopUntilIdle());

  // Check that the related child device is removed from the parent device.
  EXPECT_FALSE(parent.HasChildren());
}

TEST_F(DeviceTest, AddChildToBindableDevice) {
  auto endpoints = fidl::CreateEndpoints<fdf::Node>();

  // Create a node.
  TestNode node(dispatcher());
  auto binding = fidl::BindServer(dispatcher(), std::move(endpoints->server), &node);

  // Create a device.
  zx_protocol_device_t ops{};
  compat::Device parent(compat::kDefaultDevice, &ops, nullptr, std::nullopt, logger(),
                        dispatcher());

  // Try to a child device.
  device_add_args_t args{.name = "child"};
  zx_device_t* child = nullptr;
  ASSERT_EQ(ZX_OK, parent.Add(&args, &child));
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, child->CreateNode());
}

TEST_F(DeviceTest, GetProtocolFromDevice) {
  // Create a device without a get_protocol hook.
  zx_protocol_device_t ops{};
  compat::Device without(compat::kDefaultDevice, &ops, nullptr, std::nullopt, logger(),
                         dispatcher());
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, without.GetProtocol(ZX_PROTOCOL_BLOCK, nullptr));

  // Create a device with a get_protocol hook.
  ops.get_protocol = [](void* ctx, uint32_t proto_id, void* protocol) {
    EXPECT_EQ(ZX_PROTOCOL_BLOCK, proto_id);
    return ZX_OK;
  };
  compat::Device with(compat::kDefaultDevice, &ops, nullptr, std::nullopt, logger(), dispatcher());
  ASSERT_EQ(ZX_OK, with.GetProtocol(ZX_PROTOCOL_BLOCK, nullptr));
}

TEST_F(DeviceTest, GetFidlProtocol) {
  // Set up a fake incoming /svc.
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  service::OutgoingDirectory outgoing(dispatcher());
  outgoing.root_dir()->AddEntry(fidl::DiscoverableProtocolName<test_placeholders::Echo>,
                                fbl::MakeRefCounted<EchoImpl>(dispatcher()));
  ASSERT_EQ(ZX_OK, outgoing.Serve(std::move(endpoints->server)).status_value());

  // Set up the driver namespace.
  fidl::Arena arena;
  fidl::VectorView<fuchsia_component_runner::wire::ComponentNamespaceEntry> entries;
  entries.Allocate(arena, 1);
  entries[0].Allocate(arena);
  entries[0].set_path(arena, "/svc").set_directory(std::move(endpoints->client));
  auto ret = driver::Namespace::Create(entries);
  ASSERT_EQ(ZX_OK, ret.status_value());
  auto ns = std::move(ret.value());

  auto [node, node_client] = CreateTestNode();

  auto drv_logger = driver::Logger::Create(ns, dispatcher(), "test-logger");
  ASSERT_EQ(ZX_OK, drv_logger.status_value());
  compat::Driver drv(dispatcher(), {std::move(node_client), dispatcher()}, std::move(ns),
                     std::move(*drv_logger), "fuchsia-boot:///#meta/fake-driver.cm",
                     compat::kDefaultDevice, nullptr);

  compat::Device dev(compat::kDefaultDevice, nullptr, &drv, std::nullopt, logger(), dispatcher());

  auto echo_endpoints = fidl::CreateEndpoints<test_placeholders::Echo>();
  ASSERT_EQ(ZX_OK, echo_endpoints.status_value());

  ASSERT_EQ(ZX_OK, device_connect_fidl_protocol(
                       dev.ZxDevice(), fidl::DiscoverableProtocolName<test_placeholders::Echo>,
                       echo_endpoints->server.TakeChannel().release()));

  fidl::WireClient client(std::move(echo_endpoints->client), dispatcher());
  bool done = false;
  client->EchoString("hello").Then(
      [&done](fidl::WireUnownedResult<test_placeholders::Echo::EchoString>& result) {
        if (!result.ok()) {
          FAIL() << result.error();
          return;
        }
        auto* response = result.Unwrap();
        ASSERT_STREQ("hello", response->response.begin());
        done = true;
      });

  ASSERT_TRUE(RunLoopUntilIdle());
  ASSERT_TRUE(done);
}

TEST_F(DeviceTest, DeviceMetadata) {
  // Create a device.
  zx_protocol_device_t ops{};
  compat::Device device(compat::kDefaultDevice, &ops, nullptr, std::nullopt, logger(),
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

TEST_F(DeviceTest, DeviceFragmentMetadata) {
  // Create a device.
  zx_protocol_device_t ops{};
  compat::Device device(compat::kDefaultDevice, &ops, nullptr, std::nullopt, logger(),
                        dispatcher());

  // Add metadata to the device.
  const uint64_t metadata = 0xAABBCCDDEEFF0011;
  zx_status_t status = device.AddMetadata(DEVICE_METADATA_PRIVATE, &metadata, sizeof(metadata));
  ASSERT_EQ(ZX_OK, status);

  // Get the metadata.
  uint64_t found = 0;
  size_t found_size = 0;
  status = device_get_fragment_metadata(device.ZxDevice(), "fragment-name", DEVICE_METADATA_PRIVATE,
                                        &found, sizeof(found), &found_size);
  ASSERT_EQ(ZX_OK, status);
  EXPECT_EQ(metadata, found);
  EXPECT_EQ(sizeof(metadata), found_size);
}

TEST_F(DeviceTest, GetFragmentProtocolFromDevice) {
  // Create a device with a get_protocol hook.
  zx_protocol_device_t ops{};
  ops.get_protocol = [](void* ctx, uint32_t proto_id, void* protocol) {
    EXPECT_EQ(ZX_PROTOCOL_BLOCK, proto_id);
    return ZX_OK;
  };
  compat::Device with(compat::kDefaultDevice, &ops, nullptr, std::nullopt, logger(), dispatcher());
  ASSERT_EQ(ZX_OK, device_get_fragment_protocol(with.ZxDevice(), "fragment-name", ZX_PROTOCOL_BLOCK,
                                                nullptr));
}

TEST_F(DeviceTest, DevfsVnodeGetTopologicalPath) {
  auto endpoints = fidl::CreateEndpoints<fdf::Node>();

  // Create a device.
  zx_protocol_device_t ops{};
  compat::Device device(compat::kDefaultDevice, &ops, nullptr, std::nullopt, logger(),
                        dispatcher());
  device.Bind({std::move(endpoints->client), dispatcher()});

  // The root device doesn't have a valid topological path, so we add a child.
  zx_device_t* second_device;
  device_add_args_t args{
      .name = "second-device",
  };
  device.Add(&args, &second_device);

  DevfsVnode vnode(second_device);
  auto dev_endpoints = fidl::CreateEndpoints<fuchsia_device::Controller>();
  ASSERT_EQ(ZX_OK, endpoints.status_value());

  fidl::BindServer(test_loop().dispatcher(), std::move(dev_endpoints->server), &vnode);

  fidl::WireClient<fuchsia_device::Controller> client;
  client.Bind(std::move(dev_endpoints->client), test_loop().dispatcher());

  bool callback_called = false;
  client->GetTopologicalPath().Then(
      [&callback_called](
          fidl::WireUnownedResult<fuchsia_device::Controller::GetTopologicalPath>& result) {
        if (!result.ok()) {
          FAIL() << result.error();
          return;
        }
        auto* response = result.Unwrap();
        ASSERT_TRUE(response->result.is_response());
        std::string path(response->result.response().path.data(),
                         response->result.response().path.size());
        EXPECT_STREQ("/dev/second-device", path.data());
        callback_called = true;
      });

  ASSERT_TRUE(test_loop().RunUntilIdle());
  ASSERT_TRUE(callback_called);
}

TEST_F(DeviceTest, DevfsVnodeTestBind) {
  auto [node, node_client] = CreateTestNode();

  // Create a device.
  zx_protocol_device_t ops{};
  compat::Device device(compat::kDefaultDevice, &ops, nullptr, std::nullopt, logger(),
                        dispatcher());
  device.Bind({std::move(node_client), dispatcher()});

  size_t add_count = 0;
  node->SetAddChildHook([&add_count](TestNode::AddChildRequestView& request) {
    const char* key = "fuchsia.compat.LIBNAME";
    fidl::StringView view = fidl::StringView::FromExternal(key);
    auto object = fidl::ObjectView<fidl::StringView>::FromExternal(&view);

    if (!add_count) {
      // Check prop is not set.
      ASSERT_EQ(std::nullopt,
                GetProperty(request->args, fdf::wire::NodePropertyKey::WithStringValue(object)));
    } else {
      // Check prop is set.
      auto prop = GetProperty(request->args, fdf::wire::NodePropertyKey::WithStringValue(object));
      ASSERT_NE(std::nullopt, prop);
      ASSERT_TRUE(prop->is_string_value());
      std::string_view value{prop->string_value().data(), prop->string_value().size()};
      ASSERT_EQ("gpt.so", value);
    }

    add_count++;
  });

  zx_device_t* second_device;
  device_add_args_t args{
      .name = "second-device",
  };
  ASSERT_EQ(ZX_OK, device.Add(&args, &second_device));
  ASSERT_EQ(ZX_OK, second_device->CreateNode());

  auto [vnode, client] = CreateVnode(second_device);
  bool callback_called = false;
  client->Bind("gpt.so").Then(
      [&callback_called](fidl::WireUnownedResult<fuchsia_device::Controller::Bind>& result) {
        if (!result.ok()) {
          FAIL() << result.error();
          return;
        }
        auto* response = result.Unwrap();
        ASSERT_TRUE(response->result.is_response());
        callback_called = true;
      });

  ASSERT_TRUE(test_loop().RunUntilIdle());
  ASSERT_TRUE(callback_called);
}

TEST_F(DeviceTest, DevfsVnodeTestBindAlreadyBound) {
  auto [node, node_client] = CreateTestNode();
  // Create a device.
  zx_protocol_device_t ops{};
  compat::Device device(compat::kDefaultDevice, &ops, nullptr, std::nullopt, logger(),
                        dispatcher());
  device.Bind({std::move(node_client), dispatcher()});

  zx_device_t* second_device;
  device_add_args_t args{
      .name = "second-device",
  };
  device.Add(&args, &second_device);
  auto [node2, node2_client] = CreateTestNode();
  second_device->Bind({std::move(node2_client), dispatcher()});

  // create another device.
  zx_device_t* third_device;
  second_device->Add(&args, &third_device);

  auto [vnode, client] = CreateVnode(second_device);
  bool got_reply = false;
  client->Bind("gpt.so").Then(
      [&got_reply](fidl::WireUnownedResult<fuchsia_device::Controller::Bind>& result) {
        if (!result.ok()) {
          FAIL() << "Bind failed: " << result.error();
          return;
        }
        auto* response = result.Unwrap();
        ASSERT_TRUE(response->result.is_err());
        ASSERT_EQ(ZX_ERR_ALREADY_BOUND, response->result.err());
        got_reply = true;
      });

  ASSERT_TRUE(test_loop().RunUntilIdle());
  ASSERT_TRUE(got_reply);
}

TEST_F(DeviceTest, DevfsVnodeTestRebind) {
  auto [node, node_client] = CreateTestNode();
  // Create a device.
  zx_protocol_device_t ops{};
  compat::Device device(compat::kDefaultDevice, &ops, nullptr, std::nullopt, logger(),
                        dispatcher());
  device.Bind({std::move(node_client), dispatcher()});

  size_t add_count = 0;
  node->SetAddChildHook([&add_count](TestNode::AddChildRequestView& request) {
    const char* key = "fuchsia.compat.LIBNAME";
    fidl::StringView view = fidl::StringView::FromExternal(key);
    auto object = fidl::ObjectView<fidl::StringView>::FromExternal(&view);

    if (!add_count) {
      // Check prop is not set.
      ASSERT_EQ(std::nullopt,
                GetProperty(request->args, fdf::wire::NodePropertyKey::WithStringValue(object)));
    } else {
      // Check prop is set.
      auto prop = GetProperty(request->args, fdf::wire::NodePropertyKey::WithStringValue(object));
      ASSERT_NE(std::nullopt, prop);
      ASSERT_TRUE(prop->is_string_value());
      std::string_view value{prop->string_value().data(), prop->string_value().size()};
      ASSERT_EQ("gpt.so", value);
    }

    add_count++;
  });

  zx_device_t* second_device;
  device_add_args_t args{
      .name = "second-device",
  };
  ASSERT_EQ(ZX_OK, device.Add(&args, &second_device));
  ASSERT_EQ(ZX_OK, second_device->CreateNode());

  bool got_reply = false;
  auto [vnode, client] = CreateVnode(second_device);
  client->Rebind("gpt.so").Then(
      [&got_reply](fidl::WireUnownedResult<fuchsia_device::Controller::Rebind>& result) {
        if (!result.ok()) {
          FAIL() << "Rebind failed: " << result.error();
          return;
        }
        auto* response = result.Unwrap();
        ASSERT_TRUE(response->result.is_response());
        got_reply = true;
      });

  ASSERT_TRUE(test_loop().RunUntilIdle());
  ASSERT_TRUE(got_reply);
}
