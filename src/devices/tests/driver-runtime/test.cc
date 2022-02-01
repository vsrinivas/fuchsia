// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.device.runtime.test/cpp/wire.h>
#include <fuchsia/driver/test/cpp/fidl.h>
#include <lib/driver_test_realm/realm_builder/cpp/lib.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/synchronous_interface_ptr.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>

#include <sdk/lib/device-watcher/cpp/device-watcher.h>

using fuchsia_device_runtime_test::TestDevice;
using fuchsia_device_runtime_test::TestDeviceChild;

class RuntimeTest : public gtest::TestLoopFixture {
 protected:
  void SetUp() override {
    // Create and build the realm.
    auto realm_builder = component_testing::RealmBuilder::Create();
    driver_test_realm::Setup(realm_builder);
    realm_ = std::make_unique<component_testing::RealmRoot>(realm_builder.Build(dispatcher()));

    // Start DriverTestRealm.
    fidl::SynchronousInterfacePtr<fuchsia::driver::test::Realm> driver_test_realm;
    ASSERT_EQ(ZX_OK, realm_->Connect(driver_test_realm.NewRequest()));
    fuchsia::driver::test::Realm_Start_Result realm_result;
    ASSERT_EQ(ZX_OK, driver_test_realm->Start(fuchsia::driver::test::RealmArgs(), &realm_result));
    ASSERT_FALSE(realm_result.is_err());

    // Connect to dev.
    fidl::InterfaceHandle<fuchsia::io::Directory> dev;
    zx_status_t status = realm_->Connect("dev", dev.NewRequest().TakeChannel());
    ASSERT_EQ(status, ZX_OK);

    fbl::unique_fd root_fd;
    status = fdio_fd_create(dev.TakeChannel().release(), root_fd.reset_and_get_address());
    ASSERT_EQ(status, ZX_OK);

    fbl::unique_fd parent_fd;
    ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile(root_fd, "sys/test/parent", &parent_fd));
    ASSERT_EQ(ZX_OK, fdio_get_service_handle(parent_fd.release(),
                                             parent_chan.channel().reset_and_get_address()));
    ASSERT_TRUE(parent_chan.is_valid());

    fbl::unique_fd child_fd;
    ASSERT_EQ(ZX_OK,
              device_watcher::RecursiveWaitForFile(root_fd, "sys/test/parent/child", &child_fd));
    ASSERT_EQ(ZX_OK, fdio_get_service_handle(child_fd.release(),
                                             child_chan.channel().reset_and_get_address()));
    ASSERT_TRUE(child_chan.is_valid());
  }

  // Sets test data in the parent device that can be retrieved by the child device.
  void ParentSetTestData(const void* data_to_send, size_t size);
  // Sends a FIDL request to the child device to retrieve data from the parent device
  // using its runtime channel.
  // Asserts that the data matches |want_data| and |want_size|.
  void GetParentDataOverRuntimeChannel(bool sync, const void* want_data, size_t want_size);

  fidl::ClientEnd<TestDeviceChild> child_chan;
  fidl::ClientEnd<TestDevice> parent_chan;

 private:
  std::unique_ptr<component_testing::RealmRoot> realm_;
};

void RuntimeTest::ParentSetTestData(const void* data_to_send, size_t size) {
  fidl::Arena arena;
  fidl::VectorView<uint8_t> data(arena, size);
  memcpy(data.mutable_data(), data_to_send, size);
  data.set_count(size);

  auto response = fidl::WireCall<TestDevice>(parent_chan)->SetTestData(std::move(data));
  ASSERT_EQ(ZX_OK, response.status());
  zx_status_t call_status = ZX_OK;
  if (response->result.is_err()) {
    call_status = response->result.err();
  }
  ASSERT_EQ(call_status, ZX_OK);
}

void RuntimeTest::GetParentDataOverRuntimeChannel(bool sync, const void* want_data,
                                                  size_t want_size) {
  auto response =
      fidl::WireCall<TestDeviceChild>(child_chan)->GetParentDataOverRuntimeChannel(sync);
  ASSERT_EQ(ZX_OK, response.status());

  zx_status_t call_status = ZX_OK;
  if (response->result.is_err()) {
    call_status = response->result.err();
  }
  ASSERT_EQ(call_status, ZX_OK);

  auto data = response->result.response().out;
  ASSERT_EQ(data.count(), want_size);
  ASSERT_EQ(memcmp(data.data(), want_data, want_size), 0);
}

TEST_F(RuntimeTest, TransferOverRuntimeChannel) {
  const std::string kTestString = "some test string";
  ASSERT_NO_FATAL_FAILURE(ParentSetTestData(kTestString.c_str(), kTestString.length()));
  ASSERT_NO_FATAL_FAILURE(
      GetParentDataOverRuntimeChannel(false, kTestString.c_str(), kTestString.length()));

  const std::string kTestString2 = "another test string";
  ASSERT_NO_FATAL_FAILURE(ParentSetTestData(kTestString2.c_str(), kTestString.length()));
  ASSERT_NO_FATAL_FAILURE(
      GetParentDataOverRuntimeChannel(false, kTestString2.c_str(), kTestString.length()));
}

TEST_F(RuntimeTest, TransferOverRuntimeChannelSync) {
  const std::string kTestString = "sync call";
  ASSERT_NO_FATAL_FAILURE(ParentSetTestData(kTestString.c_str(), kTestString.length()));
  ASSERT_NO_FATAL_FAILURE(
      GetParentDataOverRuntimeChannel(true, kTestString.c_str(), kTestString.length()));
}
