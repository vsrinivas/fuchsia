// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.examples.echo/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/ddk/binding_priv.h>
#include <lib/ddk/driver.h>
#include <lib/fidl/llcpp/connect_service.h>
#include <lib/zx/status.h>
#include <lib/zx/vmo.h>

#include <algorithm>

#include <ddktl/device.h>
#include <zxtest/zxtest.h>

#include "lib/fidl/llcpp/wire_messaging.h"
#include "src/devices/testing/mock-ddk/mock-device.h"
#include "zircon/system/ulib/async-loop/include/lib/async-loop/cpp/loop.h"
#include "zircon/system/ulib/async-loop/include/lib/async-loop/loop.h"

TEST(MockDdk, BasicOps) {
  zx_protocol_device_t ops = {};
  device_add_args_t device_args = {};
  device_args.name = "test-driver";
  device_args.ops = &ops;

  zx_device_t* device;
  auto parent = MockDevice::FakeRootParent();  // Hold on to the parent during the test.
                                               // Releasing the parent will release all children.
  EXPECT_EQ(0, parent->child_count());
  EXPECT_OK(device_add_from_driver(nullptr, parent.get(), &device_args, &device));
  EXPECT_EQ(1, parent->child_count());
  // the device has no state to cleanup, so we can just dump it on the floor
}

TEST(MockDdk, InitOps) {
  zx_protocol_device_t ops = {};  // Important to initialize functions with nullptr!
  ops.init = [](void* ctx) { device_init_reply(*static_cast<zx_device_t**>(ctx), ZX_OK, nullptr); };
  device_add_args_t device_args = {};
  device_args.name = "test-driver";
  device_args.ops = &ops;
  zx_device_t* device;  // this will be set by device_add_from_driver.
  device_args.ctx = &device;

  auto parent = MockDevice::FakeRootParent();
  EXPECT_OK(device_add_from_driver(nullptr, parent.get(), &device_args, &device));

  device->InitOp();  // The "device"'s init call should send back the init_reply.

  EXPECT_TRUE(device->InitReplyCalled());
  EXPECT_EQ(ZX_OK, device->InitReplyCallStatus());
}

TEST(MockDdk, ClientRemote) {
  constexpr zx_handle_t kFakeHandle = 4;
  zx_protocol_device_t ops = {};
  device_add_args_t device_args = {};
  device_args.name = "test-driver";
  device_args.ops = &ops;
  device_args.client_remote = kFakeHandle;

  zx_device_t* device;
  auto parent = MockDevice::FakeRootParent();  // Hold on to the parent during the test.
                                               // Releasing the parent will release all children.
  EXPECT_OK(device_add_from_driver(nullptr, parent.get(), &device_args, &device));
  zx::channel request1 = device->TakeClientRemote();
  ASSERT_TRUE(request1.is_valid());
  // That should have reset the handle stored by the device:
  zx::channel request2 = device->TakeClientRemote();
  ASSERT_FALSE(request2.is_valid());
  EXPECT_EQ(request1.release(), kFakeHandle);
}

const std::array kProps = {
    zx_device_prop_t{0, 1, 2},
};
const std::array kStrProps = {
    zx_device_str_prop_t{.key = "key1", .property_value = str_prop_str_val("value")},
    zx_device_str_prop_t{.key = "key2", .property_value = str_prop_int_val(10)}};

class TestDevice;
using DeviceType = ddk::Device<TestDevice, ddk::Unbindable, ddk::Initializable, ddk::Suspendable>;
class TestDevice : public DeviceType {
 public:
  TestDevice(zx_device_t* parent) : DeviceType(parent), parent_(parent) {
    // this could call device_get_metadata on its parent, or
    // device_get_protocol on its parent.
    // It might call load_firmware.
    // These will all be separate functions in this class for testing purposes.
  }

  // Bind call from which would come from the driver:
  static zx::status<TestDevice*> Bind(zx_device_t* parent) {
    auto dev = std::make_unique<TestDevice>(parent);
    // The device_add_args_t will be filled out by the
    // base class.
    auto status = dev->DdkAdd(
        ddk::DeviceAddArgs("my-test-device").set_props(kProps).set_str_props(kStrProps));
    // The MockDevice is now in charge of the memory for dev
    if (status == ZX_OK) {
      return zx::ok(dev.release());
    }
    return zx::error(status);
  }

  zx::status<mock_ddk::Protocol> GetProtocol(uint32_t proto_id) {
    mock_ddk::Protocol protocol;
    auto status = device_get_protocol(parent_, proto_id, &protocol);
    if (status == ZX_OK) {
      return zx::ok(protocol);
    }
    return zx::error(status);
  }

  zx::status<zx::channel> ConnectToProtocol(const char* protocol_name) {
    zx::channel client, server;
    auto status = zx::channel::create(0, &client, &server);
    if (status != ZX_OK) {
      return zx::error(status);
    }

    status = device_connect_fidl_protocol(parent_, protocol_name, server.release());
    if (status == ZX_OK) {
      return zx::ok(std::move(client));
    }
    return zx::error(status);
  }

  zx::status<zx::channel> ConnectToFragmentProtocol(const char* fragment_name,
                                                    const char* protocol_name) {
    zx::channel client, server;
    auto status = zx::channel::create(0, &client, &server);
    if (status != ZX_OK) {
      return zx::error(status);
    }

    status = device_connect_fragment_fidl_protocol(parent_, fragment_name, protocol_name,
                                                   server.release());
    if (status == ZX_OK) {
      return zx::ok(std::move(client));
    }
    return zx::error(status);
  }

  zx::status<std::vector<uint8_t>> GetMetadata(uint32_t type, size_t max_size) {
    std::vector<uint8_t> data(max_size);
    size_t actual;
    auto status = device_get_metadata(parent_, type, data.data(), max_size, &actual);
    if (status == ZX_OK) {
      data.resize(actual);
      return zx::ok(data);
    }
    return zx::error(status);
  }

  zx::status<std::string> GetVariable(const char* name, size_t max_size) {
    std::vector<char> data(max_size, '\0');
    size_t actual;
    auto status = device_get_variable(parent_, name, data.data(), max_size, &actual);
    if (status == ZX_OK) {
      return zx::ok(std::string(data.data(), actual));
    }
    return zx::error(status);
  }

  zx::status<std::vector<uint8_t>> LoadFirmware(std::string_view path) {
    size_t actual;
    zx::vmo fw;
    auto status = load_firmware_from_driver(nullptr, zxdev(), std::string(path).c_str(),
                                            fw.reset_and_get_address(), &actual);
    if (status != ZX_OK) {
      return zx::error(status);
    }
    std::vector<uint8_t> data(actual);
    status = fw.read(data.data(), 0, actual);
    if (status != ZX_OK) {
      return zx::error(status);
    }
    return zx::ok(data);
  }

  zx::status<size_t> GetMetadataSize(uint32_t type) {
    size_t actual;
    auto status = device_get_metadata_size(parent_, type, &actual);
    if (status == ZX_OK) {
      return zx::ok(actual);
    }
    return zx::error(status);
  }

  // Add a child device with this device as the parent:
  zx::status<TestDevice*> AddChild() {
    auto result = Bind(zxdev());
    if (result.is_ok()) {
      children_.push_back(result.value());
    }
    return result;
  }

  // Removes a child, if one exists:
  void RemoveChild() {
    if (children_.empty())
      return;
    device_async_remove(children_.back()->zxdev());
    children_.pop_back();
  }

  // Methods required by the ddk mixins
  void DdkInit(ddk::InitTxn txn) { txn.Reply(ZX_OK); }
  void DdkUnbind(::ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkSuspend(ddk::SuspendTxn txn) { txn.Reply(ZX_OK, 0); }
  void DdkRelease() {
    // DdkRelease must delete this before it returns.
    delete this;
  }

 private:
  zx_device_t* parent_;
  std::vector<TestDevice*> children_;
};

TEST(MockDdk, CreateTestDevice) {
  auto parent = MockDevice::FakeRootParent();  // Hold on to the parent during the test.
  auto result = TestDevice::Bind(parent.get());
  ASSERT_TRUE(result.is_ok());
  // We could use the pointer we got out of the bind function.
  // However, if that is not possible, we can also get it from the parent:
  auto& test_dev_from_bind = result.value();
  ASSERT_EQ(1, parent->child_count());                   // make sure the child device is there
  MockDevice* child = parent->children().front().get();  // Get the child from the parent
  // turn the MockDevice into a TestDevice:
  TestDevice* test_dev_from_parent = child->GetDeviceContext<TestDevice>();
  EXPECT_EQ(test_dev_from_bind, test_dev_from_parent);

  // Alternatively, you can use GetLatestChild:
  auto* child2 = parent->GetLatestChild();
  ASSERT_NE(nullptr, child2);
  EXPECT_EQ(test_dev_from_bind, child2->GetDeviceContext<TestDevice>());

  // The state of the tree is now:
  //         parent
  //           |
  //         child
  EXPECT_EQ(0, child->child_count());
  // the device has no state to cleanup, so we can just dump it on the floor
}

TEST(MockDdk, TestDeviceCalls) {
  auto parent = MockDevice::FakeRootParent();  // Hold on to the parent during the test.
  auto result = TestDevice::Bind(parent.get());
  ASSERT_TRUE(result.is_ok());
  auto* child = parent->GetLatestChild();

  // MockDevice will track when calls have been made to the device manager:
  EXPECT_FALSE(child->InitReplyCalled());
  // You can trigger calls from Device Manager to the device:
  child->InitOp();  // Calls DdkInit() on the device.
  // Now InitReply should be called:
  EXPECT_TRUE(child->InitReplyCalled());

  // MockDevice will automatically call Release() on all devices when the parent is removed.
  // However, if you wanted to test device removal, here is how it would work:
  child->UnbindOp();
  // If the device has an asynchronous unbind callback, you can call:
  EXPECT_EQ(ZX_OK, child->WaitUntilUnbindReplyCalled());
  // Otherwise, you can just verify it was called:
  EXPECT_TRUE(child->UnbindReplyCalled());

  child->ReleaseOp();
  // The TestDevice and the MockDevice should now be deleted:
  ASSERT_EQ(0, parent->child_count());
}

TEST(MockDdk, TestMultipleDevices) {
  auto parent = MockDevice::FakeRootParent();  // Hold on to the parent during the test.
  auto result = TestDevice::Bind(parent.get());
  ASSERT_TRUE(result.is_ok());
  auto* test_device_0 = result.value();

  // Now add a child to the test device:
  result = test_device_0->AddChild();
  ASSERT_TRUE(result.is_ok());
  auto* test_device_1 = result.value();

  // We can get their MockDevices:
  MockDevice* child = test_device_0->zxdev();
  MockDevice* grandchild = test_device_1->zxdev();

  // The state of the tree is now:
  //         parent   <--  FakeRootParent
  //           |
  //         child    <--  test_device_0
  //           |
  //       grandchild <--  test_device_1
  EXPECT_EQ(1, parent->child_count());
  EXPECT_EQ(1, child->child_count());
  EXPECT_EQ(0, grandchild->child_count());
  EXPECT_EQ(2, parent->descendant_count());

  EXPECT_EQ(test_device_0->zxdev(), test_device_1->parent());
  // Now, say test_device_0 is able to dynamically remove its children.
  // For fun, we'll add children under test_device_1 as well:
  ASSERT_TRUE(test_device_1->AddChild().is_ok());
  ASSERT_TRUE(test_device_1->AddChild().is_ok());
  EXPECT_EQ(2, grandchild->child_count());
  EXPECT_EQ(4, parent->descendant_count());

  // To test this you would call:
  EXPECT_FALSE(grandchild->AsyncRemoveCalled());
  // Trigger the behavior that should remove a child:
  test_device_0->RemoveChild();
  EXPECT_TRUE(grandchild->AsyncRemoveCalled());
  // Because mock_ddk is not a fake, the device will not be automatically removed.
  // To get the same behavior as the device host, you must manually propagate unbind
  // and release calls:
  // 1) recursively unbind
  grandchild->UnbindOp();
  for (auto& td_child : grandchild->children()) {
    td_child->UnbindOp();
    // you would then unbind all of td_child's children, etc
  }
  // 2) wait for unbind replies, and release after receiving device_unbind_reply().
  while (!grandchild->children().empty()) {
    auto& td_child = grandchild->children().back();
    // for (auto& td_child : grandchild->children()) {
    // First you would wait for all of td_child's children to be unbound and released.
    EXPECT_EQ(ZX_OK, td_child->WaitUntilUnbindReplyCalled());
    td_child->ReleaseOp();
  }
  grandchild->ReleaseOp();

  // Now test_device_1 and its children should be fully removed.
  EXPECT_EQ(1, parent->child_count());
  EXPECT_EQ(0, child->child_count());
  EXPECT_EQ(1, parent->descendant_count());

  // A helper function has been provided for this operation.
  // Re-create some more devices:
  result = test_device_0->AddChild();
  ASSERT_TRUE(result.is_ok());
  auto* test_device_2 = result.value();
  ASSERT_TRUE(test_device_2->AddChild().is_ok());
  ASSERT_TRUE(test_device_2->AddChild().is_ok());
  EXPECT_EQ(2, test_device_2->zxdev()->child_count());
  EXPECT_EQ(4, parent->descendant_count());

  // So, if we remove the child:
  test_device_0->RemoveChild();

  // if we want the unbind-reply-release cycle to take place, call:
  EXPECT_OK(mock_ddk::ReleaseFlaggedDevices(parent.get()));

  // and viola, any device below and including parent are unbound and released
  // if device_async_remove has been called on them.
  EXPECT_EQ(1, parent->child_count());
  EXPECT_EQ(0, child->child_count());
  EXPECT_EQ(1, parent->descendant_count());

  // If any devices remain at the end of the test, ReleaseOp will be called
  // recursively on the device tree.  If a driver needs to have Unbind called to ensure
  // proper cleanup, the test writer must call UnbindOp manually.
}

TEST(MockDdk, SetMetadata) {
  auto parent = MockDevice::FakeRootParent();  // Hold on to the parent during the test.
  auto result = TestDevice::Bind(parent.get());
  ASSERT_TRUE(result.is_ok());
  TestDevice* test_device = result.value();

  constexpr uint32_t kFakeMetadataType = 4;
  constexpr uint32_t kFakeMetadataType2 = 5;
  constexpr uint32_t kFakeMetadataSize = 1000;

  // As expected, there is no default metadata available in devices:
  auto metadata_result = test_device->GetMetadata(kFakeMetadataType, kFakeMetadataSize);
  ASSERT_FALSE(metadata_result.is_ok());
  auto metadata_size_result = test_device->GetMetadataSize(kFakeMetadataType);
  ASSERT_FALSE(metadata_size_result.is_ok());

  // If your driver requires metadata, you can add it to the parent:
  // (This could be done before the device is added)
  const char kSource[] = "test";
  parent->SetMetadata(kFakeMetadataType, kSource, sizeof(kSource));

  metadata_result = test_device->GetMetadata(kFakeMetadataType, kFakeMetadataSize);
  ASSERT_TRUE(metadata_result.is_ok());
  EXPECT_EQ(metadata_result.value().size(), sizeof(kSource));
  ASSERT_BYTES_EQ(metadata_result.value().data(), kSource, sizeof(kSource));
  // get_metadata_size also works:
  metadata_size_result = test_device->GetMetadataSize(kFakeMetadataType);
  ASSERT_TRUE(metadata_size_result.is_ok());
  EXPECT_EQ(metadata_size_result.value(), sizeof(kSource));

  // Setting metadata allows the metadata to be accessed when querying for that type only:
  auto bad_metadata_result = test_device->GetMetadata(0, kFakeMetadataSize);
  ASSERT_FALSE(bad_metadata_result.is_ok());

  // Metadata propagates to children, regardless of when the child is added:
  result = test_device->AddChild();
  ASSERT_TRUE(result.is_ok());
  auto* test_device_1 = result.value();

  auto metadata_result_1 = test_device_1->GetMetadata(kFakeMetadataType, kFakeMetadataSize);
  ASSERT_TRUE(metadata_result_1.is_ok());
  EXPECT_EQ(metadata_result_1.value().size(), sizeof(kSource));
  ASSERT_BYTES_EQ(metadata_result_1.value().data(), kSource, sizeof(kSource));

  // Multiple metadata blobs can be loaded, but they overwrite previously loaded metadata
  // with the same type.
  // Because metadata is propagated to children, if you desire different metadata than a
  // parent, load the child's metadata after loading the parent's metadata.

  // Add a different blob to a different type:
  const char kSource2[] = "Hello";
  parent->SetMetadata(kFakeMetadataType2, kSource2, sizeof(kSource2));

  // Add a different blob to a the same type, but lower in the tree:
  const char kSource3[] = "World";
  test_device->zxdev()->SetMetadata(kFakeMetadataType, kSource3, sizeof(kSource3));

  // Now the devices each have two metadata blobs available,
  metadata_result = test_device->GetMetadata(kFakeMetadataType, kFakeMetadataSize);
  ASSERT_TRUE(metadata_result.is_ok());
  ASSERT_EQ(metadata_result.value().size(), sizeof(kSource));
  ASSERT_BYTES_EQ(metadata_result.value().data(), kSource, sizeof(kSource));

  metadata_result = test_device->GetMetadata(kFakeMetadataType2, kFakeMetadataSize);
  ASSERT_TRUE(metadata_result.is_ok());
  ASSERT_EQ(metadata_result.value().size(), sizeof(kSource2));
  ASSERT_BYTES_EQ(metadata_result.value().data(), kSource2, sizeof(kSource2));

  // but test_device_1 has a different value for kFakeMetadataType.
  metadata_result = test_device_1->GetMetadata(kFakeMetadataType, kFakeMetadataSize);
  ASSERT_TRUE(metadata_result.is_ok());
  ASSERT_EQ(metadata_result.value().size(), sizeof(kSource3));
  ASSERT_BYTES_EQ(metadata_result.value().data(), kSource3, sizeof(kSource3));
}

TEST(MockDdk, SetVariable) {
  auto parent = MockDevice::FakeRootParent();  // Hold on to the parent during the test.
  auto result = TestDevice::Bind(parent.get());
  ASSERT_TRUE(result.is_ok());
  TestDevice* test_device = result.value();

  constexpr char kFakeVarName[] = "foo";
  constexpr char kFakeVarName2[] = "bar";
  constexpr size_t kFakeVarSize = 10;

  // As expected, there is no default variable available in devices:
  auto variable_result = test_device->GetVariable(kFakeVarName, kFakeVarSize);
  ASSERT_FALSE(variable_result.is_ok());

  // If your driver requires variable, you can add it to the parent:
  // (This could be done before the device is added)
  const char kSource[] = "test";
  parent->SetVariable(kFakeVarName, kSource);

  variable_result = test_device->GetVariable(kFakeVarName, kFakeVarSize);
  ASSERT_TRUE(variable_result.is_ok());
  EXPECT_EQ(variable_result.value().size(), sizeof(kSource) - 1);
  ASSERT_BYTES_EQ(variable_result.value().data(), kSource, sizeof(kSource));

  // Setting variable allows the variable to be accessed when querying for that type only:
  auto bad_variable_result = test_device->GetVariable("", kFakeVarSize);
  ASSERT_FALSE(bad_variable_result.is_ok());

  // Multiple variable blobs can be loaded, but they overwrite previously loaded variable
  // with the same name.

  // Add a different blob to a different type:
  const char kSource2[] = "Hello";
  parent->SetVariable(kFakeVarName2, kSource2);

  // Add a different blob to a the same type, but lower in the tree:
  const char kSource3[] = "World";
  test_device->zxdev()->SetVariable(kFakeVarName, kSource3);

  // Now the devices each have two variable blobs available,
  variable_result = test_device->GetVariable(kFakeVarName, kFakeVarSize);
  ASSERT_TRUE(variable_result.is_ok());
  ASSERT_EQ(variable_result.value().size(), sizeof(kSource) - 1);
  ASSERT_BYTES_EQ(variable_result.value().data(), kSource, sizeof(kSource));

  variable_result = test_device->GetVariable(kFakeVarName2, kFakeVarSize);
  ASSERT_TRUE(variable_result.is_ok());
  ASSERT_EQ(variable_result.value().size(), sizeof(kSource2) - 1);
  ASSERT_BYTES_EQ(variable_result.value().data(), kSource2, sizeof(kSource2));

  // but test_device_1 has a different value for kFakeVarName.
  result = test_device->AddChild();
  ASSERT_TRUE(result.is_ok());
  auto* test_device_1 = result.value();
  variable_result = test_device_1->GetVariable(kFakeVarName, kFakeVarSize);
  ASSERT_TRUE(variable_result.is_ok());
  ASSERT_EQ(variable_result.value().size(), sizeof(kSource3) - 1);
  ASSERT_BYTES_EQ(variable_result.value().data(), kSource3, sizeof(kSource3));
}

struct test_math_protocol_ops {
  void (*domath)(void* ctx, int in, int* out);
};

// Many devices communicate with their parents and / or children through banjo protocols.
// If your device requires a banjo protocol you can load one into its parent.
TEST(MockDdk, SetProtocol) {
  auto parent = MockDevice::FakeRootParent();  // Hold on to the parent during the test.
  auto result = TestDevice::Bind(parent.get());
  ASSERT_TRUE(result.is_ok());
  TestDevice* test_device = result.value();

  constexpr uint32_t kFakeProtocolID = 4;
  constexpr uint32_t kFakeProtocolID2 = 5;

  // Initially, the device will fail to get a protocol:
  EXPECT_FALSE(test_device->GetProtocol(kFakeProtocolID).is_ok());

  // So we add the necessary protocol to the parent:
  test_math_protocol_ops math_ops = {.domath = [](void* ctx, int in, int* out) { *out = in + 1; }};

  parent->AddProtocol(kFakeProtocolID, &math_ops, nullptr);

  // Protocol is available after being set.
  auto proto_result = test_device->GetProtocol(kFakeProtocolID);
  EXPECT_TRUE(proto_result.is_ok());
  EXPECT_EQ(proto_result.value().ops, &math_ops);

  // Incorrect proto ids still fail.
  EXPECT_FALSE(test_device->GetProtocol(kFakeProtocolID2).is_ok());
}

class EchoServer : fidl::WireServer<fidl_examples_echo::Echo> {
 public:
  void Bind(async_dispatcher_t* dispatcher, fidl::ServerEnd<fidl_examples_echo::Echo> request) {
    fidl::BindServer<fidl::WireServer<fidl_examples_echo::Echo>>(dispatcher, std::move(request),
                                                                 this);
  }

 private:
  void EchoString(EchoStringRequestView request, EchoStringCompleter::Sync& completer) override {
    completer.Reply(request->value);
  }
};

TEST(MockDdk, SetFidlProtocol) {
  auto parent = MockDevice::FakeRootParent();  // Hold on to the parent during the test.
  auto result = TestDevice::Bind(parent.get());
  ASSERT_OK(result.status_value());
  TestDevice* test_device = result.value();

  constexpr char kFakeProtocolName[] = "Foo";
  constexpr auto kEchoProtocolName = fidl::DiscoverableProtocolName<fidl_examples_echo::Echo>;

  // Initially, the device will fail to get a protocol:
  EXPECT_FALSE(test_device->ConnectToProtocol(kEchoProtocolName).is_ok());

  async::Loop loop{&kAsyncLoopConfigNeverAttachToThread};
  EchoServer server;
  // So we add the necessary protocol to the parent:
  parent->AddFidlProtocol(kEchoProtocolName, [&](zx::channel request) {
    server.Bind(loop.dispatcher(), fidl::ServerEnd<fidl_examples_echo::Echo>(std::move(request)));
    return ZX_OK;
  });

  // Protocol is available after being set.
  auto proto_result = test_device->ConnectToProtocol(kEchoProtocolName);
  ASSERT_OK(proto_result.status_value());
  ASSERT_TRUE(proto_result.value().is_valid());
  fidl::WireClient client(fidl::ClientEnd<fidl_examples_echo::Echo>(std::move(*proto_result)),
                          loop.dispatcher());

  constexpr std::string_view kInput = "Test String";

  client->EchoString(fidl::StringView::FromExternal(kInput),
                     [&](fidl::WireUnownedResult<fidl_examples_echo::Echo::EchoString>& result) {
                       EXPECT_OK(result.status());
                       EXPECT_EQ(result->response.get(), kInput);
                     });
  EXPECT_OK(loop.RunUntilIdle());

  // Incorrect proto ids still fail.
  EXPECT_FALSE(test_device->ConnectToProtocol(kFakeProtocolName).is_ok());
}

// Fragments are devices that allow for protocols to come from different parents.
TEST(MockDdk, SetFragments) {
  auto parent = MockDevice::FakeRootParent();  // Hold on to the parent during the test.
  auto result = TestDevice::Bind(parent.get());
  ASSERT_TRUE(result.is_ok());
  TestDevice* test_device = result.value();

  constexpr uint32_t kFakeProtocolID = 4;
  constexpr uint32_t kFakeProtocolID2 = 5;

  // Initially, the device will fail to get a protocol:
  EXPECT_FALSE(test_device->GetProtocol(kFakeProtocolID).is_ok());

  test_math_protocol_ops math_ops = {.domath = [](void* ctx, int in, int* out) { *out = in + 1; }};
  test_math_protocol_ops math_ops2 = {.domath = [](void* ctx, int in, int* out) { *out = in - 1; }};
  // You can add protocols to new or existing fragments using AddProtocol:
  parent->AddProtocol(kFakeProtocolID, &math_ops, nullptr, "fragment 1");
  parent->AddProtocol(kFakeProtocolID2, &math_ops2, nullptr, "fragment 2");

  // Now, when querying the normal protocols, the device will fail to get a protocol:
  mock_ddk::Protocol protocol;
  EXPECT_NE(device_get_protocol(parent.get(), kFakeProtocolID, &protocol), ZX_OK);

  // But if you query a fragment protocol, it can succeed:
  auto status =
      device_get_fragment_protocol(parent.get(), "fragment 1", kFakeProtocolID, &protocol);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(protocol.ops, &math_ops);

  status = device_get_fragment_protocol(parent.get(), "fragment 2", kFakeProtocolID2, &protocol);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(protocol.ops, &math_ops2);

  // As expected, device_get_fragment_protocol will fail if you request a protocol id
  // that is not present in the fragment, or a non-existing fragment:
  // non-existing fragment:
  EXPECT_NE(
      device_get_fragment_protocol(parent.get(), "not a fragment", kFakeProtocolID, &protocol),
      ZX_OK);

  // Mismatched fragment / protocol id:
  EXPECT_NE(device_get_fragment_protocol(parent.get(), "fragment 1", kFakeProtocolID2, &protocol),
            ZX_OK);

  // Mismatched fragment / protocol id:
  EXPECT_NE(device_get_fragment_protocol(parent.get(), "fragment 2", kFakeProtocolID, &protocol),
            ZX_OK);
}

// Fragments are devices that allow for protocols to come from different parents.
TEST(MockDdk, SetFragmentsFidl) {
  auto parent = MockDevice::FakeRootParent();  // Hold on to the parent during the test.
  auto result = TestDevice::Bind(parent.get());
  ASSERT_TRUE(result.is_ok());
  TestDevice* test_device = result.value();

  constexpr char kFakeProtocolName[] = "Foo";
  constexpr char kFakeProtocolName2[] = "Bar";

  // Initially, the device will fail to get a protocol:
  EXPECT_FALSE(test_device->ConnectToProtocol(kFakeProtocolName).is_ok());

  // You can add protocols to new or existing fragments using AddProtocol:
  parent->AddFidlProtocol(
      kFakeProtocolName, [](zx::channel) { return ZX_OK; }, "fragment 1");
  parent->AddFidlProtocol(
      kFakeProtocolName2, [](zx::channel) { return ZX_OK; }, "fragment 2");

  // Now, when querying the normal protocols, the device will fail to get a protocol:
  EXPECT_FALSE(test_device->ConnectToProtocol(kFakeProtocolName).is_ok());

  // But if you query a fragment protocol, it can succeed:
  auto proto_result = test_device->ConnectToFragmentProtocol("fragment 1", kFakeProtocolName);
  EXPECT_TRUE(proto_result.is_ok());
  EXPECT_TRUE(proto_result.value().is_valid());

  proto_result = test_device->ConnectToFragmentProtocol("fragment 2", kFakeProtocolName2);
  EXPECT_TRUE(proto_result.is_ok());
  EXPECT_TRUE(proto_result.value().is_valid());

  // As expected, device_get_fragment_protocol will fail if you request a protocol id
  // that is not present in the fragment, or a non-existing fragment:
  // non-existing fragment:
  EXPECT_FALSE(test_device->ConnectToFragmentProtocol("not a fragment", kFakeProtocolName).is_ok());

  // Mismatched fragment / protocol id:
  EXPECT_FALSE(test_device->ConnectToFragmentProtocol("fragment 1", kFakeProtocolName2).is_ok());

  // Mismatched fragment / protocol id:
  EXPECT_FALSE(test_device->ConnectToFragmentProtocol("fragment 2", kFakeProtocolName).is_ok());
}

// In case a device loads firmware as part of its initialization, MockDevice provides
// a way to set firmware that can be accessed by the load_firmware call.
TEST(MockDdk, LoadFirmware) {
  auto parent = MockDevice::FakeRootParent();  // Hold on to the parent during the test.
  auto result = TestDevice::Bind(parent.get());
  ASSERT_TRUE(result.is_ok());
  TestDevice* test_device = result.value();

  constexpr std::string_view kFirmwarePath = "test path";
  constexpr std::string_view kFirmwarePath2 = "test path2";
  std::vector<uint8_t> kFirmware(200, 42);

  // Initially, the device will fail to get a protocol:
  EXPECT_FALSE(test_device->LoadFirmware(kFirmwarePath).is_ok());

  // So we add the necessary firmware:
  test_device->zxdev()->SetFirmware(kFirmware, kFirmwarePath);

  // firmware is available after being set.
  auto firmware_result = test_device->LoadFirmware(kFirmwarePath);
  EXPECT_TRUE(firmware_result.is_ok());
  ASSERT_EQ(firmware_result.value().size(), kFirmware.size());
  ASSERT_BYTES_EQ(firmware_result.value().data(), kFirmware.data(), kFirmware.size());

  // Incorrect firmware paths still fail.
  EXPECT_FALSE(test_device->LoadFirmware(kFirmwarePath2).is_ok());
  // unless we add the firmware path as empty:
  test_device->zxdev()->SetFirmware(kFirmware, "");
  // Then any path will match:
  firmware_result = test_device->LoadFirmware(kFirmwarePath2);
  EXPECT_TRUE(firmware_result.is_ok());
  ASSERT_EQ(firmware_result.value().size(), kFirmware.size());
  ASSERT_BYTES_EQ(firmware_result.value().data(), kFirmware.data(), kFirmware.size());
}

TEST(MockDdk, SetSize) {
  auto parent = MockDevice::FakeRootParent();  // Hold on to the parent during the test.

  // Initially, the size will be 0.
  EXPECT_EQ(device_get_size(parent.get()), 0);

  // So we size in the parent:
  parent->SetSize(32);

  // Size should be 32 after being set.
  EXPECT_EQ(device_get_size(parent.get()), 32);
}

TEST(MockDdk, GetProperties) {
  auto parent = MockDevice::FakeRootParent();  // Hold on to the parent during the test.
  auto result = TestDevice::Bind(parent.get());
  ASSERT_TRUE(result.is_ok());
  TestDevice* test_device = result.value();

  ASSERT_EQ(test_device->zxdev()->GetProperties().size(), kProps.size());
  ASSERT_EQ(memcmp(test_device->zxdev()->GetProperties().data(), kProps.data(), kProps.size()), 0);
  ASSERT_EQ(test_device->zxdev()->GetStringProperties().size(), cpp20::span(kStrProps).size());
  ASSERT_EQ(memcmp(test_device->zxdev()->GetStringProperties().data(), kStrProps.data(),
                   kStrProps.size()),
            0);
}

TEST(MockDdk, GetInspectVmo) {
  auto parent = MockDevice::FakeRootParent();  // Hold on to the parent during the test.
  auto dev = std::make_unique<TestDevice>(parent.get());
  zx::vmo inspect;
  ASSERT_OK(zx::vmo::create(1024, 0, &inspect));
  zx_handle_t handle = inspect.get();
  ASSERT_OK(dev->DdkAdd(ddk::DeviceAddArgs("my-test-device").set_inspect_vmo(std::move(inspect))));
  // The MockDevice is now in charge of the memory for dev
  TestDevice* test_device = dev.release();

  ASSERT_EQ(test_device->zxdev()->GetInspectVmo().get(), handle);
}
