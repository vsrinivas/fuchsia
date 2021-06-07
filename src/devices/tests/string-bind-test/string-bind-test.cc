// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/device/manager/cpp/fidl.h>
#include <fuchsia/device/test/llcpp/fidl.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/driver.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <lib/sys/cpp/component_context.h>

#include <gtest/gtest.h>

#include "src/lib/fxl/strings/string_printf.h"

const std::string kDevPrefix = "/dev/";
const std::string kDriverTestDir = "/boot/driver/test";
const std::string kDriverLibname = "string-bind-parent.so";
const std::string kDriverLibPath = kDriverTestDir + "/" + kDriverLibname;
const std::string kStringBindDriverLibPath = kDriverTestDir + "/string-bind-child.so";
const std::string kChildDeviceName = "child";

using devmgr_integration_test::IsolatedDevmgr;

class StringBindTest : public testing::Test {
 protected:
  void SetUp() override {
    auto args = IsolatedDevmgr::DefaultArgs();

    args.driver_search_paths.push_back("/boot/driver");

    ASSERT_EQ(IsolatedDevmgr::Create(std::move(args), &devmgr_), ZX_OK);
    ASSERT_NE(devmgr_.svc_root_dir().channel(), ZX_HANDLE_INVALID);

    // Wait for /dev/test/test to appear, then get a channel to it.
    fbl::unique_fd root_fd;
    ASSERT_EQ(ZX_OK, devmgr_integration_test::RecursiveWaitForFile(devmgr_.devfs_root(),
                                                                   "test/test", &root_fd));

    auto root_device_endpoints = fidl::CreateEndpoints<fuchsia_device_test::RootDevice>();
    ASSERT_EQ(ZX_OK, root_device_endpoints.status_value());

    auto root_device = fidl::BindSyncClient(std::move(root_device_endpoints->client));
    auto status = fdio_get_service_handle(root_fd.release(),
                                          root_device.mutable_channel()->reset_and_get_address());
    ASSERT_EQ(ZX_OK, status);

    auto endpoints = fidl::CreateEndpoints<fuchsia_device::Controller>();
    ASSERT_EQ(ZX_OK, endpoints.status_value());

    // Create the root test device in /dev/test/test, and get its relative path from /dev.
    auto result = root_device.CreateDevice(fidl::StringView::FromExternal(kDriverLibname),
                                           endpoints->server.TakeChannel());

    ASSERT_EQ(ZX_OK, result.status());

    ASSERT_GE(result->path.size(), kDevPrefix.size());
    ASSERT_EQ(strncmp(result->path.data(), kDevPrefix.c_str(), kDevPrefix.size()), 0);
    relative_device_path_ = std::string(result->path.data() + kDevPrefix.size(),
                                        result->path.size() - kDevPrefix.size());

    // Bind the test driver to the new device.
    auto response =
        fidl::WireCall(endpoints->client).Bind(::fidl::StringView::FromExternal(kDriverLibPath));
    status = response.status();
    if (status == ZX_OK) {
      if (response->result.is_err()) {
        status = response->result.err();
      }
    }
    ASSERT_EQ(ZX_OK, status);

    // Wait for the child device to bind and appear. The child device should bind with its string
    // properties.
    fbl::unique_fd string_bind_fd;
    status = devmgr_integration_test::RecursiveWaitForFile(
        devmgr_.devfs_root(), "test/test/string-bind-parent/child", &string_bind_fd);
    ASSERT_EQ(ZX_OK, status);

    // Connect to the BindDebugger service.
    auto bind_endpoints = fidl::CreateEndpoints<fuchsia::device::manager::BindDebugger>();
    ASSERT_EQ(ZX_OK, bind_endpoints.status_value());

    std::string svc_name =
        fxl::StringPrintf("svc/%s", fuchsia::device::manager::BindDebugger::Name_);
    sys::ServiceDirectory svc_dir(devmgr_.TakeSvcRootDir().TakeChannel());
    status = svc_dir.Connect(svc_name, bind_endpoints->server.TakeChannel());
    ASSERT_EQ(ZX_OK, status);

    bind_debugger_.Bind(bind_endpoints->client.TakeChannel());
  }

  IsolatedDevmgr devmgr_;
  fuchsia::device::manager::BindDebuggerSyncPtr bind_debugger_;
  std::string relative_device_path_;
};

// Get the bind program of the test driver and check that it has the expected instructions.
TEST_F(StringBindTest, DriverBytecode) {
  fuchsia::device::manager::BindDebugger_GetBindRules_Result result;
  ASSERT_EQ(ZX_OK, bind_debugger_->GetBindRules(kStringBindDriverLibPath, &result));
  ASSERT_TRUE(result.is_response());
  auto bytecode = result.response().bind_rules.bytecode_v2();

  const uint8_t kExpectedBytecode[] = {
      0x42, 0x49, 0x4E, 0x44, 0x02, 0x0,  0x0,  0x0,               // Bind header
      0x53, 0x59, 0x4E, 0x42, 0x45, 0x00, 0x00, 0x00,              // Symbol table header
      0x01, 0x0,  0x0,  0x0,                                       // "stringbind.lib.kinglet" ID
      0x73, 0x74, 0x72, 0x69, 0x6e, 0x67, 0x62, 0x69, 0x6e, 0x64,  // "stringbind"
      0x2e, 0x6c, 0x69, 0x62, 0x2e, 0x6b, 0x69, 0x6e, 0x67, 0x6c,  // ".lib.kingl"
      0x65, 0x74, 0x00,                                            // "et"
      0x02, 0x00, 0x00, 0x00,                                      // "firecrest" ID
      0x66, 0x69, 0x72, 0x65, 0x63, 0x72, 0x65, 0x73, 0x74, 0x00,  // "firecrest"
      0x03, 0x00, 0x00, 0x00,                                      // "stringbind.lib.bobolink" ID
      0x73, 0x74, 0x72, 0x69, 0x6e, 0x67, 0x62, 0x69, 0x6e, 0x64,  // "stringbind"
      0x2e, 0x6c, 0x69, 0x62, 0x2e, 0x62, 0x6f, 0x62, 0x6f, 0x6c,  // ".lib.bobol"
      0x69, 0x6e, 0x6b, 0x00,                                      // "ink"
      0x49, 0x4E, 0x53, 0x54, 0x21, 0x00, 0x00, 0x00,              // Instruction header
      0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x00, 0x00,
      0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,
      0x01, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x0a, 0x00, 0x00, 0x00,
  };

  ASSERT_EQ(countof(kExpectedBytecode), bytecode.size());
  for (size_t i = 0; i < bytecode.size(); i++) {
    ASSERT_EQ(kExpectedBytecode[i], bytecode[i]);
  }
}

TEST_F(StringBindTest, DeviceProperties) {
  std::string child_device_path(relative_device_path_ + "/" + kChildDeviceName);

  fuchsia::device::manager::BindDebugger_GetDeviceProperties_Result result;
  ASSERT_EQ(ZX_OK, bind_debugger_->GetDeviceProperties(child_device_path, &result));

  ASSERT_TRUE(result.is_response());

  constexpr zx_device_prop_t kExpectedProps[] = {
      {BIND_PROTOCOL, 0, 3},
      {BIND_PCI_VID, 0, 1234},
      {BIND_PCI_DID, 0, 1234},
  };

  auto props = result.response().property_list.props;
  ASSERT_EQ(props.size(), countof(kExpectedProps));
  for (size_t i = 0; i < props.size(); i++) {
    ASSERT_EQ(props[i].id, kExpectedProps[i].id);
    ASSERT_EQ(props[i].reserved, kExpectedProps[i].reserved);
    ASSERT_EQ(props[i].value, kExpectedProps[i].value);
  }

  auto& str_props = result.response().property_list.str_props;
  ASSERT_EQ(static_cast<size_t>(2), str_props.size());

  ASSERT_STREQ("stringbind.lib.kinglet", str_props[0].key.data());
  ASSERT_TRUE(str_props[0].value.is_str_value());
  ASSERT_STREQ("firecrest", str_props[0].value.str_value().data());

  ASSERT_STREQ("stringbind.lib.bobolink", str_props[1].key.data());
  ASSERT_TRUE(str_props[1].value.is_int_value());
  ASSERT_EQ(static_cast<uint32_t>(10), str_props[1].value.int_value());
}
