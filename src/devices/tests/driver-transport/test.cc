// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.transport.test/cpp/wire.h>
#include <fuchsia/driver/test/cpp/fidl.h>
#include <lib/driver_test_realm/realm_builder/cpp/lib.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/synchronous_interface_ptr.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>

#include "sdk/lib/device-watcher/cpp/device-watcher.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

using fuchsia_driver_transport_test::TestDevice;
using fuchsia_driver_transport_test::TestDeviceChild;

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
                                             parent_client.channel().reset_and_get_address()));
    ASSERT_TRUE(parent_client.is_valid());

    fbl::unique_fd child_fd;
    ASSERT_EQ(ZX_OK,
              device_watcher::RecursiveWaitForFile(root_fd, "sys/test/parent/child", &child_fd));
    ASSERT_EQ(ZX_OK, fdio_get_service_handle(child_fd.release(),
                                             child_client.channel().reset_and_get_address()));
    ASSERT_TRUE(child_client.is_valid());
  }

  // Sets test data in the parent device that can be retrieved by the child device.
  void ParentSetTestData(uint8_t* data_to_send, size_t size);
  // Sends a FIDL request to the child device to retrieve data from the parent device
  // using its runtime channel.
  // Asserts that the data matches |want_data| and |want_size|.
  void GetParentDataOverDriverTransport(const void* want_data, size_t want_size);

  fidl::ClientEnd<TestDeviceChild> child_client;
  fidl::ClientEnd<TestDevice> parent_client;

 private:
  std::unique_ptr<component_testing::RealmRoot> realm_;
};

void RuntimeTest::ParentSetTestData(uint8_t* data_to_send, size_t size) {
  auto data = fidl::VectorView<uint8_t>::FromExternal(data_to_send, size);

  auto response = fidl::WireCall(parent_client)->SetTestData(std::move(data));
  ASSERT_EQ(ZX_OK, response.status());
  zx_status_t call_status = ZX_OK;
  if (response->is_error()) {
    call_status = response->error_value();
  }
  ASSERT_EQ(call_status, ZX_OK);
}

void RuntimeTest::GetParentDataOverDriverTransport(const void* want_data, size_t want_size) {
  auto response = fidl::WireCall(child_client)->GetParentDataOverDriverTransport();
  ASSERT_EQ(ZX_OK, response.status());

  zx_status_t call_status = ZX_OK;
  if (response->is_error()) {
    call_status = response->error_value();
  }
  ASSERT_EQ(call_status, ZX_OK);

  auto data = response->value()->out;
  ASSERT_EQ(data.count(), want_size);
  ASSERT_EQ(memcmp(data.data(), want_data, want_size), 0);
}

TEST_F(RuntimeTest, TransferOverDriverTransport) {
  uint8_t kTestString[] = "some test string";
  ASSERT_NO_FATAL_FAILURE(ParentSetTestData(kTestString, std::size(kTestString)));
  ASSERT_NO_FATAL_FAILURE(GetParentDataOverDriverTransport(kTestString, std::size(kTestString)));

  uint8_t kTestString2[] = "another test string";
  ASSERT_NO_FATAL_FAILURE(ParentSetTestData(kTestString2, std::size(kTestString)));
  ASSERT_NO_FATAL_FAILURE(GetParentDataOverDriverTransport(kTestString2, std::size(kTestString)));
}
